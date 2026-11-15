# HANDOFF — fork talentsnest/ctrpad

Fork de chrissotraidis/ctrpad pour une seule chose : **les téléphones comme manettes** (projet
talentsnest/manette-tel). Branche de travail : `feat/manette-reseau`. `main` reste le miroir d'upstream.

## Ce qui a été ajouté (15.11.2026)

- `platform/native_netpad.c` : écoute UDP 127.0.0.1:55400-55403, protocole « Network RetroPad » de RetroArch
  (struct `remote_message`, 20 octets). Chaque joueur devient une manette virtuelle SDL, attachée à son premier
  paquet et dans l'ordre (joueur 1 → pad 1…). Le chemin manette existant gère slots, multitap et mapping.
  Désactivable avec `CTR_NETPAD=0`, ports déplaçables avec `CTR_NETPAD_PORT`. Desktop seulement (pas iOS/Windows).
- Branché dans `Platform_InputInit` / `Platform_InputUpdate` / `Platform_InputShutdown` (`native_input.c`),
  inclus dans le build unity par `main.c`.
- Test `--self-test-netpad` (ctest `ctr_native_netpad`) : 3 joueurs → pads 1-3, multitap actif, Croix / Haut /
  R2 / stick arrivent sur le bus, relâchement effacé. Suite complète : 32/32.

## Correspondance des boutons (page manette → PlayStation)

B (bas) = Croix, Y (gauche) = Carré, A (droite) = Rond, X (haut) = Triangle, L/R = L1/R1, L2/R2 = gâchettes,
Select/Start, croix directionnelle, joystick = stick gauche.

## Compiler et lancer

```sh
brew install cmake ninja
cmake --preset macos-arm64-app && cmake --build --preset macos-arm64-app
build-macos-arm64-app/CTRPad.app/Contents/MacOS/CTRPad --self-test-netpad
```

Puis `Lancer CTRPad.command` dans manette-tel. Il faut une image NTSC-U du jeu, choisie au premier lancement.

## Pas encore vérifié

- Une vraie partie à 3 (pas d'image du jeu sur ce Mac). Le multijoueur de CTRPad n'est pas validé upstream.
- Au premier lancement, macOS demande l'accès au dossier Documents (le build est dans ~/Documents) : à accepter.

## Règles du projet upstream (AGENTS.md)

Ne jamais publier de build (app, zip, IPA), ni d'image disque, de données extraites ou de sauvegarde.
Le fork ne contient que du code source.
