# MKA Core Audio

## 1. Objectif

MKA Core Audio est une couche d’abstraction audio temps réel destinée à fournir une interface unifiée avec différents systèmes et backends audio.

Elle masque les API spécifiques aux backends tout en fournissant une interface simple, prévisible et indépendante du backend aux couches supérieures.

Core Audio est responsable du transport de l’audio entre l’application et le système audio sous-jacent. Il ne traite ni n’interprète le signal audio.

## 2. Objectifs principaux

Core Audio doit :

* fournir une interface commune aux différents backends audio ;
* énumérer les périphériques disponibles ;
* exposer leurs capacités ;
* permettre à l’application de choisir une configuration de stream ;
* ouvrir et gérer les streams audio ;
* fournir des buffers audio temps réel ;
* rester indépendant de toute API de backend particulière ;
* garantir un comportement déterministe et compatible avec les contraintes temps réel.

Core Audio doit rester petit, stable et prévisible.

## 3. Modèle conceptuel

Core Audio repose sur trois concepts principaux :

```text
Backend
   │
   ├── Endpoints
   │
   └── Stream
        ├── Input Channels
        └── Output Channels
```

### Endpoint

Un device représente une ressource ou un point d'accès audio sélectionnable exposé par un backend.

Il ne représente pas nécessairement du matériel physique. Le backend traduit ses propres concepts vers le modèle générique `Endpoint`.

### Stream

Un stream représente une session audio active ouverte avec une configuration précise pour un device.

La configuration du stream est immuable pendant toute sa durée de vie.

Modifier la configuration nécessite de fermer le stream actuel et d'en ouvrir un nouveau.

### Channel

Un channel représente un signal audio transporté par un stream.

Les concepts propres aux backends, tels que les channels PCM d’ALSA, les ports de PipeWire ou les éléments de WASAPI/CoreAudio, ne doivent pas apparaître dans l’interface publique de Core Audio.

## 4. Découverte des périphériques et de leurs capacités

La découverte des périphériques et l’ouverture d’un stream sont deux opérations distinctes.

L’API publique se trouve dans `mka::audio::core` et est réexportée par
`mka.audio.backend.abstract`. La découverte retourne identité et capacités ensemble :

```cpp
std::vector<Endpoint> Backend::getEndPoints() const noexcept;
```

`Endpoint` contient `std::string id`, `std::string name` et
`std::optional<StreamCapabilities> input` / `output`. Il n’existe pas de méthode
séparée de consultation des capacités. Chaque `StreamCapabilities` contient
`minChannels`, `maxChannels`, `sampleRates`, `formats` et `bufferSizes`.

Les capacités peuvent notamment décrire :

* les fréquences d’échantillonnage supportées ;
* les formats audio supportés ;
* le nombre de channels d’entrée et de sortie supportés ;
* les tailles de buffer supportées ou leurs limites ;
* les autres contraintes nécessaires à l’ouverture d’un stream.

Les capacités décrivent ce que le backend est capable de fournir. Elles ne constituent pas une configuration de stream.

## 5. Configuration du stream

C’est l’application qui choisit la configuration souhaitée.

Par exemple :

```cpp
using namespace mka::audio::core;

EndpointConfig config{
    .id = "...",
    .direction = Direction::Output,
    .inputChannels = 0,
    .outputChannels = 2,
    .sampleRate = 48000,
    .format = Format::Float32,
    .bufferSize = 256
};
```

Cette configuration constitue une demande explicite adressée au backend.

### Règle de configuration stricte

`open(config)` doit demander **exactement** la configuration spécifiée.

Core Audio ne doit pas modifier, approximer ou négocier automatiquement la configuration demandée.

Si le backend ne peut pas fournir cette configuration, `open()` doit échouer avec une erreur appropriée.

Par exemple :

```text
Demandé :
    48000 Hz
    Float32
    256 frames
    2 channels d'entrée
    2 channels de sortie

Backend :
    Compatible

Résultat :
    OPEN
```

Si la configuration n'est pas supportée :

```text
Demandé :
    48000 Hz
    Float32
    256 frames

Backend :
    Configuration non supportée

Résultat :
    ERROR
```

Le backend ne doit pas remplacer silencieusement la configuration par une autre, par exemple `44100 Hz` ou `512 frames`.

Cela garantit que la configuration réellement utilisée est connue et contrôlée par l’application.

L’application peut consulter les `Endpoint` retournés par `getEndPoints()` avant `open()` afin de vérifier que la configuration demandée est supportée.

Le backend doit néanmoins effectuer une nouvelle validation lors de `open()`, car les capacités peuvent avoir changé entre leur découverte et l’ouverture du stream.

## 6. Cycle de vie d’un stream

Le cycle de vie minimal est :

```text
CLOSED
   │ open()
   ▼
OPEN
   │ start()
   ▼
RUNNING
   │ stop()
   ▼
OPEN
   │ close()
   ▼
CLOSED
```

L’interface publique minimale est :

```cpp
getEndPoints()

open(config)
start()
stop()
close()
```

La configuration ne peut pas être modifiée pendant qu’un stream est ouvert.

Une modification de configuration nécessite :

```text
stop()
   ↓
close()
   ↓
open(newConfig)
   ↓
start()
```

Tout mécanisme de hot-swap ou de reconfiguration transparente est de la responsabilité de l’`AudioEngine`, et non de Core Audio.

## 7. Buffer audio

Core Audio fournit les buffers audio aux composants de niveau supérieur.

Un buffer générique peut être représenté ainsi :

```cpp
struct InputBuffer {
    const float* const* channels;
    std::uint32_t count;
};
struct OutputBuffer {
    float* const* channels;
    std::uint32_t count;
};
struct AudioProcessContext {
    InputBuffer input;
    OutputBuffer output;
    std::uint32_t frames;
};
using ProcessFunction = void (*)(void* user, const AudioProcessContext&) noexcept;
```

La représentation interne peut varier selon les backends.

Core Audio est responsable de traduire les données audio natives du backend vers la représentation générique.

Une conversion de format peut être effectuée lorsque cela est nécessaire à l’abstraction.

Core Audio ne doit pas imposer une taille fixe de bloc DSP. Le nombre de frames fourni par le backend peut varier.

La gestion des blocs DSP de taille fixe appartient au DSP Engine.

## 8. Contraintes temps réel

Le callback audio doit être compatible avec les contraintes temps réel.

Le chemin d’exécution temps réel doit notamment éviter :

* les allocations dynamiques ;
* les désallocations ;
* les mutex bloquants ;
* l’accès au système de fichiers ;
* les opérations réseau ;
* les appels système potentiellement bloquants ;
* les opérations dont la durée d’exécution n’est pas bornée ;
* les logs lourds ;
* toute opération présentant une latence imprévisible.

Chaque implémentation de backend doit également respecter les contraintes temps réel qui lui sont propres.

## 9. Responsabilités de Core Audio

Core Audio est responsable de :

* l’énumération des périphériques ;
* la découverte de leurs capacités ;
* la configuration des streams ;
* la création des streams ;
* leur cycle de vie ;
* le transport des données audio ;
* l’abstraction des buffers audio ;
* l’abstraction et, si nécessaire, la conversion des formats audio ;
* la remontée d’erreurs indépendante du backend ;
* la gestion du callback temps réel ;
* l’isolation des implémentations backend.

## 10. Hors périmètre

Core Audio ne doit pas gérer :

* le traitement DSP ;
* la planification des blocs DSP ;
* les plugins ;
* VST/AU ;
* les mixeurs ;
* les graphes audio ;
* les nodes ;
* les tracks ;
* les bus ;
* le routage logique audio ;
* le transport ;
* la timeline ;
* l’automation ;
* le MIDI ;
* la gestion des projets ;
* l’interface utilisateur ;
* le réseau ;
* la logique métier du DAW.

Le routage audio physique ou virtuel appartient à une couche audio de niveau supérieur.

Le traitement et la planification DSP appartiennent au DSP Engine.

## 11. Séparation des responsabilités

L’architecture suit le principe suivant :

```text
DAW Engine
    ↓
DSP Engine
    ↓
Audio Routing
    ↓
Audio Engine
    ↓
Core Audio
    ↓
Backend
    ↓
Système d’exploitation / Matériel
```

Les responsabilités sont séparées ainsi :

**Core Audio**

> Abstrait le backend et transporte l’audio.

**Audio Engine**

> Orchestre l’exécution audio temps réel et les opérations audio de niveau supérieur.

**Audio Routing**

> Détermine comment les channels physiques et virtuels sont connectés.

**DSP Engine**

> Traite l’audio et gère l’exécution DSP.

**DAW**

> Détermine ce qui doit être traité.

## 12. Indépendance vis-à-vis des backends

Core Audio ne doit exposer aucun type ou concept spécifique à un backend.

L’interface publique doit utiliser des concepts génériques tels que :

```cpp
Endpoint
StreamCapabilities
EndpointConfig
AudioProcessContext
Format
Error
```

Les implémentations peuvent utiliser en interne :

* ALSA ;
* PipeWire ;
* JACK ;
* WASAPI ;
* CoreAudio ;
* ou toute autre API native.

Ces détails doivent rester isolés du reste de l’application.

## 13. Gestion des erreurs


`Result` est exactement `std::expected<void, ErrorType>`. Le succès vaut `Result{}`,
l’échec `std::unexpected{ErrorType::...}`. Il n’existe pas de valeur `None`.
`open`, `start`, `stop`, `close`, `setProcessFunction` et `setEventHandler` retournent ce type.

| ErrorType | Signification |
| --- | --- |
| InvalidState | Opération interdite dans l’état courant ou depuis le dispatcher d’événements. |
| EndpointUnavailable | Endpoint/serveur absent, inaccessible ou déconnecté. |
| FormatNotSupported | Format inconnu ou non fourni. |
| ChannelsNotSupported | Comptes incohérents avec la direction ou non fournis. |
| SampleRateNotSupported | Fréquence nulle ou non fournie exactement au flux applicatif. |
| BufferSizeNotSupported | Taille nulle ou paramètre de transport non fourni. |
| ConfigurationFailed | Autre échec natif, direction inconnue ou exception d’un hook de contrôle. |

L’état est vérifié d’abord, puis les paramètres communs avant toute acquisition
native. Ne pas supposer un ordre universel des autres erreurs si plusieurs champs
sont invalides. Les erreurs des hooks sont transmises intactes ; leurs exceptions
deviennent `ConfigurationFailed`, sans avancer l’état. Le hook doit libérer ses
ressources partielles après un échec.

Les xruns et pertes de flux asynchrones utilisent `Status::xruns` / `Status::failed`
et `EventType::XRun` / `EventType::Failed`, jamais un retour Result du callback.
Après perte de flux, le thread de contrôle appelle `stop()` puis `close()` avant
de rouvrir. Un refus de priorité RT entraîne `RealtimeState::No`, sans échec
d’ouverture. La découverte conserve sa convention de liste vide en cas d’échec.

## 14. Critères d’acceptation

Une implémentation de Core Audio est conforme si elle :

* énumère les périphériques disponibles ;
* expose leurs capacités ;
* permet à l’application de construire une configuration explicite ;
* valide cette configuration lors de l’ouverture ;
* n’ouvre le stream qu’avec la configuration demandée ;
* échoue explicitement lorsque cette configuration n’est pas supportée ;
* supporte `open/start/stop/close` ;
* fournit des buffers audio temps réel ;
* n’impose pas de taille fixe aux blocs DSP ;
* respecte les contraintes temps réel ;
* n’expose aucun concept spécifique à un backend dans son interface publique ;
* permet à plusieurs backends d’implémenter la même interface ;
* impose la recréation du stream lorsque sa configuration change.

## 15. Principe architectural

> **Core Audio doit rester petit, stable, déterministe et prévisible.**

Le backend transporte l’audio.

Core Audio abstrait le backend.

L’Audio Engine orchestre l’exécution temps réel.

Le DSP Engine traite l’audio.

Le DAW décide de ce qui doit être traité.

Toute fonctionnalité qui n’est pas directement liée à la découverte des périphériques, à la découverte de leurs capacités, à la configuration des streams, à leur cycle de vie ou au transport audio temps réel appartient à un composant de niveau supérieur.

`EndpointConfig::direction` exige Input : `inputChannels > 0`, `outputChannels == 0` ; Output : `inputChannels == 0`, `outputChannels > 0` ; Duplex : les deux comptes strictement positifs. Des comptes incohérents retournent `ChannelsNotSupported` avant toute ouverture native. Une direction inconnue retourne `ConfigurationFailed`. Les limites de canaux propres au backend restent applicables.

## Taille de buffer et frames du callback

`bufferSize` est exprimé en frames par canal, pas en octets ni en échantillons
totaux. ALSA l’interprète comme une période (le ring en contient plusieurs) ;
JACK vérifie le quantum initial du serveur. PipeWire/PulseAudio demandent une
latence de transport ; PulseAudio l’utilise aussi comme capacité des chunks de
scratch préalloués. Ces demandes ne garantissent ni la latence du serveur ni une
taille fixe de callback. `AudioProcessContext::frames` fait foi à chaque appel :
traiter exactement ce nombre d’échantillons par canal. Core Audio n’accumule
jamais de blocs DSP fixes.

## Durée de vie des pointeurs audio

Les tables `const float* const*` d’entrée et `float* const*` de sortie sont des
vues planaires Float32 empruntées, alignées pour `float`, valides uniquement
jusqu’au retour du callback. Ne pas les conserver/libérer, modifier les tables,
ni les transmettre à un autre thread. Chaque canal actif est non nul et contient
`frames` échantillons ; un côté absent vaut `{nullptr, 0}`. L’entrée est en lecture
seule et ne recouvre pas la sortie. La sortie commence au silence à chaque appel ;
écrire uniquement dans `[0, frames)`. Sans callback, la sortie reste silencieuse.
`user` doit rester vivant jusqu’au retour de `stop()`, qui attend la fin du
callback. Les méthodes de contrôle sont interdites depuis le callback audio.

## Configuration strictement appliquée

`open(config)` valide et réserve le flux applicatif natif avant de réussir, sans
appeler `ProcessFunction`. Fréquence, format, direction et comptes de canaux doivent
correspondre à la demande, sans approximation. Un échec conserve Closed et libère
les ressources partielles. La configuration est immuable jusqu’à `close()` ; la
modifier exige `stop()` / `close()` / `open(newConfig)` / `start()`.

Cette exactitude concerne la frontière du flux applicatif : un serveur audio peut
convertir entre ce flux et son matériel. `bufferSize` conserve son sens par backend :
période ALSA, quantum initial JACK, demande de latence PipeWire/PulseAudio. Une
demande de latence ne garantit ni un ring matériel exact, ni le quantum du graphe,
ni la taille du callback, ni la latence de bout en bout. `frames` fait foi à chaque
callback. JACK revérifie fréquence et quantum avant start, le serveur pouvant
changer pendant que le client est Open.

## Capacités détectées et capacités effectives

`getEndPoints() const noexcept` s’exécute sur le thread de contrôle : cette méthode
peut allouer et communiquer avec le serveur audio ; `noexcept` ne la rend pas RT.
Elle retourne un instantané par valeur, sans réserver de ressource. L’absence de
`input` ou `output` signifie que la direction n’était pas exposée lors de cette
découverte (occupée, indisponible ou non supportée). Une liste vide peut aussi
signifier un échec de découverte ; l’interface ne possède pas de canal d’erreur
séparé pour cette opération.

| Backend | Origine réelle des capacités |
| --- | --- |
| ALSA | Bornes natives des canaux et sondes indépendantes des fréquences, formats et périodes candidats du framework. Les flux occupés/non interrogeables peuvent être omis. |
| JACK | Fréquence/quantum actuels du serveur et nombre de ports audio regroupés par client ; une sortie serveur devient une entrée du framework. |
| PipeWire | Identités du registre et candidats génériques : 1–2 canaux, Float32, listes de fréquences/buffers du framework. Aucune sonde EnumFormat fine par nœud. |
| PulseAudio | Identités sink/source et comptes de canaux rapportés par le serveur ; Float32 et candidats fréquence/buffer du framework. Le serveur peut convertir. |

Ces listes ne sont ni exhaustives ni une garantie de compatibilité de toutes leurs
combinaisons. `supportedSampleRates`, `supportedFormats` et `supportedBufferSizes`
sont des candidats du framework, pas une preuve de support matériel. `open()` doit
revérifier le flux natif et peut refuser explicitement un candidat annoncé.

## Modèle duplex par backend

`Direction::Duplex` désigne **un seul ID d’endpoint**, avec des comptes entrée/sortie
strictement positifs choisis indépendamment. Les deux sens partagent une seule
configuration et un seul `AudioProcessContext` par cycle. Deux capacités optionnelles
présentes ne prouvent pas à elles seules qu’une ouverture simultanée est possible.

| Backend | Identité et contrat duplex actuel |
| --- | --- |
| ALSA | Un ID PCM (normalement `hw:X,Y`) ouvert en capture et lecture. Les deux configurations exactes et `snd_pcm_link` doivent réussir. Sinon `ConfigurationFailed` et fermeture des deux handles, sans repli non synchronisé. |
| JACK | Un nom de client : ses ports de sortie alimentent l’entrée du framework et ses ports d’entrée reçoivent sa sortie. Les deux groupes doivent contenir assez de canaux. Un cycle serveur fournit un callback duplex. ID vide : ports client non connectés. |
| PipeWire | Un nom de nœud source ou sink. Une demande Duplex valide retourne `ConfigurationFailed` avant création native. Input et Output sont supportés séparément. |
| PulseAudio | Un nom de source ou sink. Une demande Duplex valide retourne `ConfigurationFailed` avant création native. ID vide : périphérique par défaut du sens demandé. |
| Windows/macOS | Les fichiers de backend actuels sont des stubs de modules ; aucune implémentation duplex fonctionnelle n’est revendiquée. |

ALSA ne combine pas des périphériques capture/lecture distincts ; JACK regroupe les
deux sens sous un même client. Deux instances unidirectionnelles ne constituent pas
un stream duplex synchronisé et ne garantissent ni horloge ni latence communes.
La coordination d’endpoints indépendants et le duplex des backends serveur restent
hors de ce contrat ; aucun graphe DSP n’est ajouté au Core.
