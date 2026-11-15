# Tests de contrat PipeWire

`mka-audio-pipewire-contract-tests` teste la négociation et le cycle de vie sur
le sink virtuel `mka_test_sink`. Un serveur PipeWire de bureau avec des sorties
physiques ne fournit généralement pas cette fixture. Sans elle, une ouverture
échoue avec `EndpointUnavailable` avant de vérifier le format attendu.

Depuis la racine du dépôt, compiler puis lancer les sept tests sur les services
privés prévus à cet effet :

```bash
cmake --build cmake-build-debug --target mka-audio-pipewire-contract-tests
bash test/linux/run_virtual_tests.sh --pipewire-only \
  ./cmake-build-debug/mka-audio-pipewire-contract-tests \
  --gtest_filter='*' --gtest_color=no
```

Ou sélectionner la suite avec CTest :

```bash
bash test/linux/run_virtual_tests.sh --pipewire-only \
  ctest --test-dir cmake-build-debug -R '^pipewire\.contract\.' --output-on-failure
```

Le mode `--pipewire-only` nécessite `pipewire`, `pw-cli`, `pw-cat`, `wireplumber`,
`python3` et `dbus-run-session`. Il crée un répertoire d'exécution et un bus D-Bus
privés, lance PipeWire et WirePlumber, puis les arrête à la fin des tests. Il ne
lance pas JACK ou PulseAudio. Sans cette option, le lanceur conserve la fixture
complète des quatre backends.

Dans CLion, utiliser le terminal intégré ou une configuration **Shell Script** :
script `test/linux/run_virtual_tests.sh`, arguments
`--pipewire-only ./cmake-build-debug/mka-audio-pipewire-contract-tests`, répertoire
de travail à la racine du projet. Le test et ses serveurs doivent rester dans
cette même exécution du script.

Un lancement direct sans le sink attendu affiche désormais un **SKIP** expliquant
la commande à utiliser ; ce résultat ne valide pas le backend. Si le lanceur
annonce la fixture mais qu'elle manque, le test échoue. Si le sink est présent,
les vérifications de négociation et d'ouverture continuent à échouer en cas de
régression réelle.
