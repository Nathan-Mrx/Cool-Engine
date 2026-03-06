import os
from PIL import Image

# --- CONFIGURATION ---
INPUT_DIR = "../icons"
OUTPUT_DIR = "../icons_processed"
TARGET_SIZE = 256
MARGIN = 40  # Marge un peu plus grande pour respirer dans l'UI d'ImGui

def process_icons():
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    for filename in os.listdir(INPUT_DIR):
        if filename.lower().endswith(".png"):
            filepath = os.path.join(INPUT_DIR, filename)
            try:
                # 1. Ouvrir l'image
                img = Image.open(filepath).convert("RGBA")
                r, g, b, a = img.split()

                # 2. LE KÄRCHER ANTI-PIXELS FANTÔMES
                # Tout pixel avec une opacité inférieure à 15 (sur 255) devient 0 (transparent)
                # Sinon, il garde sa valeur. Cela nettoie la "saleté" invisible.
                clean_a = a.point(lambda p: p if p > 15 else 0)

                # 3. Créer une image 100% blanche avec l'alpha nettoyé
                white_img = Image.new("RGBA", img.size, (255, 255, 255, 255))
                white_img.putalpha(clean_a)

                # 4. Calculer la vraie boîte de délimitation (Bounding Box)
                bbox = clean_a.getbbox()

                if bbox:
                    cropped = white_img.crop(bbox)

                    # 5. Calculer le ratio parfait pour redimensionner
                    max_target = TARGET_SIZE - (MARGIN * 2)
                    ratio = min(max_target / cropped.width, max_target / cropped.height)

                    new_w = int(cropped.width * ratio)
                    new_h = int(cropped.height * ratio)

                    # LANCZOS est le meilleur algorithme pour garder les icônes nettes
                    resized = cropped.resize((new_w, new_h), Image.Resampling.LANCZOS)

                    # 6. Coller au centre exact d'une toile vierge
                    final_img = Image.new("RGBA", (TARGET_SIZE, TARGET_SIZE), (0, 0, 0, 0))
                    paste_x = (TARGET_SIZE - new_w) // 2
                    paste_y = (TARGET_SIZE - new_h) // 2

                    final_img.paste(resized, (paste_x, paste_y), resized)

                    # 7. Sauvegarder
                    out_path = os.path.join(OUTPUT_DIR, filename)
                    final_img.save(out_path)
                    print(f"[OK] Uniformise et nettoye : {filename}")
                else:
                    print(f"[IGNORE] {filename} est vide ou presque invisible.")

            except Exception as e:
                print(f"[ERREUR] Sur {filename}: {e}")

if __name__ == "__main__":
    current_dir = os.path.basename(os.getcwd())
    if current_dir != "tools":
        print("Attention : Executez ce script depuis le dossier 'tools/'")
    else:
        process_icons()