# Tests audio manuels WASAPI (sprint 5)

Les deux tests `WasapiManualTest` font partie de `mka-audio-wasapi-tests` :

| Test | Comportement |
| --- | --- |
| `DISABLED_TestPlaySineWave` | Sinus de 440 Hz pendant 3 secondes, amplitude 0,2, fondus de 20 ms et silence final pour laisser jouer les buffers. |
| `DISABLED_TestRecordAndPlayback` | Capture de 5 secondes, mesures par canal, sauvegarde WAV puis relecture immédiate du canal micro le plus fort sur les sorties. |

Depuis la racine du projet, avec le build CLion Windows configuré pour WASAPI :

```powershell
powershell -NoProfile -File test/windows/run_manual_wasapi.ps1 sine cmake-build-debug
powershell -NoProfile -File test/windows/run_manual_wasapi.ps1 record cmake-build-debug
```

Le script compile la cible avant de lancer exactement le test demandé et
propage son code de sortie. Il accepte les builds Ninja/MinGW de CLion et MSVC,
ainsi que les exécutables placés dans un sous-dossier `Debug`/`Release`.
Pour créer un build MSVC depuis un terminal développeur Visual Studio :

```powershell
cmake --preset windows-wasapi
powershell -NoProfile -File test/windows/run_manual_wasapi.ps1 sine build/windows-wasapi
powershell -NoProfile -File test/windows/run_manual_wasapi.ps1 record build/windows-wasapi
```

Les périphériques console par défaut de Windows sont sélectionnés séparément
pour l'entrée et la sortie. Le nom, l'ID, le nombre de canaux, le format natif,
la fréquence et la taille de buffer demandée sont affichés. Pour changer de
micro ou de sortie, sélectionner le périphérique par défaut dans les paramètres
Son de Windows. Des IDs exacts issus de `Wasapi::getEndPoints()` peuvent aussi
être imposés :

```powershell
$env:MKA_TEST_WASAPI_INPUT_DEVICE = '<ID du microphone>'
$env:MKA_TEST_WASAPI_OUTPUT_DEVICE = '<ID de la sortie>'
$env:MKA_TEST_WASAPI_CAPTURE_WAV = 'C:\Temp\mka-wasapi-capture.wav'
powershell -NoProfile -File test/windows/run_manual_wasapi.ps1 record cmake-build-debug
```

Parler pendant la capture. Le test affiche la crête et le RMS de chaque canal
après soustraction de sa moyenne. Il sauvegarde les échantillons bruts en WAV
PCM16 multicanal, avant gain, suppression de composante continue et adaptation
de fréquence. Le chemin par défaut est `mka-wasapi-capture.wav` dans le dossier
courant. Une capture silencieuse, constante, non finie ou trop faible échoue
explicitement : crête supérieure à 0,001 et RMS supérieur à 0,0001 (-80 dBFS)
sur le meilleur canal sont requis. Vérifier la sourdine et le niveau du micro,
ainsi que l'autorisation Windows d'accès au microphone pour les applications
de bureau, si ces seuils ne sont pas atteints.

La relecture retire la composante continue, applique un gain limité à x32 et
des fondus, puis duplique le canal le plus fort vers les sorties. Si les mixes
natifs du micro et des haut-parleurs utilisent des fréquences différentes,
le clip est adapté par interpolation linéaire avant la lecture. Cette opération
appartient au test ; chaque flux WASAPI conserve son format natif exact.
Les callbacks utilisent uniquement des buffers préalloués. Les attentes sont
bornées et les compteurs de frames, xruns et échecs sont affichés.

Ces tests portent les labels CTest `integration;hardware;wasapi;manual` et sont
désactivés par défaut. Pour les lancer directement :

```powershell
.\cmake-build-debug\mka-audio-wasapi-tests.exe --gtest_filter=WasapiManualTest.DISABLED_TestPlaySineWave --gtest_also_run_disabled_tests
.\cmake-build-debug\mka-audio-wasapi-tests.exe --gtest_filter=WasapiManualTest.DISABLED_TestRecordAndPlayback --gtest_also_run_disabled_tests
```

La CI compile les deux tests avec MSVC et MinGW/GCC 15.2, vérifie leur découverte
et leur désactivation, puis teste les signaux et le WAV sans matériel. Les tests
matériels automatiques existants restent des sondes silencieuses. Une exécution
manuelle sans périphérique approprié échoue avec un diagnostic. L'écoute sur
la machine Windows confirme l'audibilité réelle de la sinusoïde et de la voix.
