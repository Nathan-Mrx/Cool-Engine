#pragma once
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "Mesh.h"
#include <memory>
#include <iostream>

class ModelLoader {
public:
    static std::shared_ptr<Mesh> LoadModel(const std::string& path) {
        std::cout << "[Cool Engine] Loading asset: " << path << std::endl;
        Assimp::Importer importer;
        // On demande à Assimp de trianguler les faces et de calculer les normales si absentes
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

        if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cerr << "ASSIMP ERROR: " << importer.GetErrorString() << std::endl;
            return nullptr;
        }

        // Pour l'instant, on ne prend que le premier mesh du fichier pour simplifier
        aiMesh* mesh = scene->mMeshes[0];
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        // Facteur de conversion : Mètres vers Centimètres
        const float importScale = 100.0f;

        for(unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;

            // On applique le Z-Up ET la conversion en centimètres
            vertex.Position = {
                mesh->mVertices[i].x * importScale,
                -mesh->mVertices[i].z * importScale,
                mesh->mVertices[i].y * importScale
            };

            // Attention : Les normales sont des directions (vecteurs unitaires),
            // elles ne doivent SURTOUT PAS être multipliées par l'échelle !
            vertex.Normal = {
                mesh->mNormals[i].x,
                -mesh->mNormals[i].z,
                mesh->mNormals[i].y
            };

            if(mesh->mTextureCoords[0])
                vertex.TexCoords = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
            else
                vertex.TexCoords = { 0.0f, 0.0f };

            vertices.push_back(vertex);
        }

        for(unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for(unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        return std::make_shared<Mesh>(vertices, indices);
    }
};