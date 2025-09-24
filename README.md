README - Contrôleur Audio DIY (Pro Micro)

Résumé
------
Ce firmware pour Pro Micro gère :
- 3 potentiomètres sur `A0`, `A1`, `A2` — envoyés périodiquement sur le port série au format :
  `POTS:v1|v2|v3|20` (valeurs 0..1023). Le `|20` est un champ fixe pour compatibilité.
- 3 boutons "principaux" (programmables) sur les pins `3`, `2`, `4`.
  Par défaut : bouton1 = PREV, bouton2 = PLAYPAUSE, bouton3 = NEXT.
- 2 boutons poussoirs (modificateurs) gauche/droite sur `5` et `6`.
  - Si gauche seul enfoncé → mode 1
  - Si gauche+droite enfoncés → mode 2
  - Sinon → mode 0
- 2 LEDs indicatrices des boutons poussoirs sur `7` (gauche) et `8` (droite) — peuvent être reconfigurées.
- Ruban NeoPixel (WS2812) de 3 LEDs, pin par défaut `16` — couleurs contrôlables et persistées.
- Configuration persistée dans l'EEPROM du microcontrôleur.

Dépendances
-----------
- Bibliothèque Arduino `Adafruit_NeoPixel` (installer via Library Manager).

- Protocole série (ASCII)
- -----------------------
- Baud : `9600`
- Fin de ligne : CR/LF
- Réponses : lignes commençant par `OK:`, `ERR:`, `POTS:`, `EVT:`, `CFG:`, `MAP:` etc.

Commandes principales (envoyer en une ligne) :
- `HELP` — liste condensée des commandes.
- `PING` — répond `PONG`.
- `SET_MAP <mode> <btn> <action>` — assigne une action à un bouton dans un mode (mode 0..2, btn 1..3, action id ou nom).
    Exemple : `SET_MAP 0 1 PREV` ou `SET_MAP 1 2 3`
- `GET_MAP` — retourne les mappings actuels, ex. `MAP:M0{1,2,3};M1{...};M2{...}`.
- `SET_LED <i> <R> <G> <B>` — change la couleur de la LED i (1..3) sur le ruban.
    Exemple : `SET_LED 1 0 255 0` pour vert.
- `SET_ALL <R> <G> <B>` — change toutes les LEDs.
- `SET_LED_PIN <pin>` — change la pin de données du ruban NeoPixel (nécessite `SAVE` pour persister).
- `SET_IND_PINS <leftPin> <rightPin>` — change les pins des LEDs indicatrices gauche/droite.
- `GET_CFG` — retourne configuration sommaire (pins).
- `SAVE` — écrit la configuration actuelle dans l'EEPROM.
- `RESET` — remet les valeurs par défaut et les sauve.

Événements envoyés par l'appareil
--------------------------------
- `POTS:v1|v2|v3|20` — transmis périodiquement (par défaut toutes les 200 ms).
- `EVT:<mode>|<btnIdx>|<action>|DOWN|UP` — quand un bouton principal change d'état.
  Exemple : `EVT:0|2|3|DOWN` = mode0, bouton 2, action 3 (NEXT), appui.

Notes de comportement
---------------------
- Les boutons sont câblés en `INPUT_PULLUP`. Appuyé = état bas (`LOW`).
- Mode déterminé par l'état des boutons poussoirs gauche/droite au moment de l'appui principal.
- Les actions sont simplement des identifiants envoyés dans l'événement — c'est au logiciel hôte d'interpréter et d'exécuter la commande (ex. contrôler un lecteur audio).

Exemples rapides (Python)
-------------------------
Voir `send_command.py` fourni pour envoyer des commandes et lire la réponse.

Fils/wiring par défaut
----------------------
- Potentiomètres -> `A0`, `A1`, `A2`
- Main buttons -> `3`, `2`, `4`
- Left push -> `5`, Right push -> `6`
- Indicator LEDs -> `7` (left), `8` (right)
- NeoPixel data -> `16`

Prochaines améliorations possibles
---------------------------------
- Ajouter ACCÈS à l'EEPROM par commande `DUMP_CFG` et `LOAD_CFG` plus détaillées.
- Supporter scènes / presets pour 3 LEDs.
- Ajouter acknowledgement détaillées (IDs de transaction) pour éviter la perte de commandes.


















