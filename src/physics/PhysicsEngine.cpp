#include "PhysicsEngine.h"
#include <iostream>
#include <cstdarg>

// --- INCLUDES JOLT ---
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

using namespace JPH;

// --- 1. DÉFINITION DES CALQUES DE COLLISION ---
namespace PhysicsLayers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
};

// Filtre : Qui rentre en collision avec qui ?
class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
        switch (inObject1) {
            case PhysicsLayers::NON_MOVING:
                return inObject2 == PhysicsLayers::MOVING; // Le statique ne touche que le dynamique
            case PhysicsLayers::MOVING:
                return true; // Le dynamique touche tout le monde
            default:
                return false;
        }
    }
};

// BroadPhase : Optimisation spatiale (quadtree/octree)
namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint32_t NUM_LAYERS(2);
};

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[PhysicsLayers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[PhysicsLayers::MOVING]     = BroadPhaseLayers::MOVING;
    }

    virtual uint32_t GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }

private:
    BroadPhaseLayer mObjectToBroadPhase[PhysicsLayers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case PhysicsLayers::NON_MOVING:
                return inLayer2 == BroadPhaseLayers::MOVING;
            case PhysicsLayers::MOVING:
                return true;
            default:
                return false;
        }
    }
};


// --- 2. STRUCTURE DE DONNÉES INTERNE ---
struct PhysicsData {
    TempAllocatorImpl* TempAllocator = nullptr;
    JobSystemThreadPool* JobSystem = nullptr;

    BPLayerInterfaceImpl BroadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl ObjectVsBroadphaseLayerFilter;
    ObjectLayerPairFilterImpl ObjectVsObjectLayerFilter;

    PhysicsSystem* m_PhysicsSystem = nullptr; // <-- Renommé ici
};

static PhysicsData* s_PhysicsData = nullptr;

// Callback pour les erreurs Jolt
static void TraceImpl(const char* inFMT, ...) { 
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    std::cout << "[Jolt Physics] " << buffer << std::endl;
}

// --- 3. IMPLÉMENTATION DU MOTEUR ---
void PhysicsEngine::Init() {
    s_PhysicsData = new PhysicsData();

    // 1. Initialisation de la mémoire et des callbacks
    RegisterDefaultAllocator();
    Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(AssertFailed = [](const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine) {
        std::cerr << "[Jolt Fatal] " << inFile << ":" << inLine << " (" << inExpression << ") " << (inMessage ? inMessage : "") << std::endl;
        return true;
    };)

    Factory::sInstance = new Factory();
    RegisterTypes();

    // 2. Initialisation des systèmes annexes (Mémoire temporaire et Multithreading)
    s_PhysicsData->TempAllocator = new TempAllocatorImpl(10 * 1024 * 1024); // 10 MB pour les calculs temporaires
    s_PhysicsData->JobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, thread::hardware_concurrency() - 1);

    // 3. Création du système physique principal
    const uint32_t cMaxBodies = 10240;
    const uint32_t cNumBodyMutexes = 0;
    const uint32_t cMaxBodyPairs = 10240;
    const uint32_t cMaxContactConstraints = 10240;

    s_PhysicsData->m_PhysicsSystem = new PhysicsSystem();
    s_PhysicsData->m_PhysicsSystem->Init(
        cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
        s_PhysicsData->BroadPhaseLayerInterface,
        s_PhysicsData->ObjectVsBroadphaseLayerFilter,
        s_PhysicsData->ObjectVsObjectLayerFilter
    );

    std::cout << "[PhysicsEngine] Jolt Physics initialized successfully." << std::endl;
}

void PhysicsEngine::Shutdown() {
    if (!s_PhysicsData) return;

    delete s_PhysicsData->m_PhysicsSystem;

    delete s_PhysicsData->JobSystem;
    delete s_PhysicsData->TempAllocator;

    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;

    delete s_PhysicsData;
    s_PhysicsData = nullptr;
    
    std::cout << "[PhysicsEngine] Jolt Physics shutdown." << std::endl;
}

void PhysicsEngine::Update(float ts) {
    if (!s_PhysicsData || !s_PhysicsData->m_PhysicsSystem) return;

    // On dit à Jolt d'avancer dans le temps.
    s_PhysicsData->m_PhysicsSystem->Update(ts, 1, s_PhysicsData->TempAllocator, s_PhysicsData->JobSystem);
}