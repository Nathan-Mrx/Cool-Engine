import os
import sys
import subprocess

def main():
    # Vérification des arguments
    if len(sys.argv) < 2:
        print("Usage: python CompileShaders.py <dossier_shaders>")
        sys.exit(1)

    shader_dir = sys.argv[1]

    # Les extensions de shaders qu'on veut surveiller
    valid_extensions = ['.vert', '.frag', '.comp']
    compiled_count = 0

    if not os.path.exists(shader_dir):
        print(f"[ShaderCompiler] Erreur : Le dossier {shader_dir} n'existe pas.")
        sys.exit(1)

    # On parcourt tous les fichiers du dossier
    for file in os.listdir(shader_dir):
        ext = os.path.splitext(file)[1]

        if ext in valid_extensions:
            source_path = os.path.join(shader_dir, file)

            # --- LE FILTRE INTELLIGENT ---
            try:
                with open(source_path, 'r', encoding='utf-8') as f:
                    # On lit les premières lignes pour être rapide
                    head = f.read(512)
                    # Si c'est du 450, ou si ça contient le tag VULKAN, on compile !
                    if "#version 450" not in head and "// VULKAN" not in head:
                        continue
            except Exception:
                continue
            # -----------------------------

            # On génère le nom de sortie : triangle.vert -> triangle_vert.spv

            # On génère le nom de sortie : triangle.vert -> triangle_vert.spv
            name = os.path.splitext(file)[0]
            out_name = f"{name}_{ext[1:]}.spv"
            out_path = os.path.join(shader_dir, out_name)

            # Logique conditionnelle : A-t-on besoin de compiler ?
            needs_compile = False
            if not os.path.exists(out_path):
                needs_compile = True # Le .spv n'existe pas encore
            elif os.path.getmtime(source_path) > os.path.getmtime(out_path):
                needs_compile = True # Le fichier source a été modifié plus récemment

            if needs_compile:
                print(f"[ShaderCompiler] Compilation de {file} -> {out_name}...")

                # Exécution de glslc
                result = subprocess.run(["glslc", source_path, "-o", out_path])

                if result.returncode != 0:
                    print(f"[ShaderCompiler] ERREUR FATALE lors de la compilation de {file}")
                    sys.exit(1) # Fait planter la compilation CMake pour t'avertir !

                compiled_count += 1

    if compiled_count == 0:
        print("[ShaderCompiler] Tous les shaders sont a jour (Skip).")
    else:
        print(f"[ShaderCompiler] {compiled_count} shader(s) recompile(s) avec succes.")

if __name__ == "__main__":
    main()