# SC4SIM.dll – SC4 System Integration Module (CAM)

DLL del **SC4 System Integration Module (SIM)** del Colossus Addon Mod. Versione **1.4.0**.

## Ricompense dei consiglieri (Lua del SIM)

Maxis ha le funzioni per creare una reward solo per tre consiglieri: `create_reward_cityplanning`,
`create_reward_finances` e `create_reward_safety`. Il SIM aggiunge le altre quattro, così qualunque mod può far
consegnare le proprie reward dal consigliere giusto, **senza installare NEON**:

| Funzione | Consigliere |
|---|---|
| `create_reward_utilities('<IID edificio>')` | Utilità |
| `create_reward_transportation('<IID edificio>')` | Trasporti |
| `create_reward_environment('<IID edificio>')` | Ambiente |
| `create_reward_health_education('<IID edificio>')` | Salute e Istruzione |

Funzionano esattamente come `create_reward_cityplanning`: record `cSC4RewardAdvice` (classe 0xAA371C32),
`once = 0`, con il tipo di consiglio di quel consigliere. Ogni funzione è definita solo se nessun altro file la
definisce già.

Le funzioni sono **Lua**, nel file `lua/sim_advisor_rewards.lua`, che il SIM installa dentro
`1 CAM Core/CAM_LUA_Controller.dat` come script CA63E2A3 / 4A5E8EF6 / **8151A000**, pacchetto Lua
**0x0151A000**. Il gioco compila gli script nell'ordine del numero di pacchetto: gli script che usano queste
funzioni devono avere un numero più alto (NEON: 0x0BB35100). Non serve la DLL per usarle: una reward chiama la
funzione mentre il gioco compila lo script, prima che una DLL possa raggiungere il Lua della città.

Esempio:

```lua
--#-package:0c000000# -- un numero più alto di 0151a000
a = create_reward_utilities('E0C896CC')
function a.condition() ... end
a.title   = [[text@...]]
a.message = [[text@...]]
```

## Cosa fa la DLL: Lua

* Tabella Lua `sim` con `sim.version()`: gli script possono sapere se la DLL è installata
  (`if sim ~= nil and sim.version ~= nil then ... end`).
* All'apertura di una città controlla che il Lua del SIM sia caricato e che ogni funzione esista, e lo scrive in
  `SC4SIM.log`, accanto alla DLL:

```
[INFO] SC4 SIM DLL 1.0.0
[INFO] SIM Lua advisor rewards 1.0 loaded.
[INFO] create_reward_utilities: available
...
```

## Tralicci elettrici personalizzati (1.1.0)

Porting nel SC4SIM.dll della modifica **custom-powerlines** di caspervg
([sc4-render-services, ramo re/custom-powerlines](https://github.com/caspervg/sc4-render-services/tree/re/custom-powerlines),
`src/sample/power-pole-customization`). Solo la modifica dei tralicci: **non** contiene SC4RenderServices né
ImGui, e non serve installarli.

* **Cavi personalizzati**: un exemplar di prop traliccio può dichiarare, per ogni direzione, quanti cavi porta
  (1–15, `MaxPointsPerDirection`) e dove si attaccano, più larghezza, flessione, texture dei cavi e delle boe e
  texture delle fondazioni (proprietà 0xB22A0000–0xB22A0013, in `docs/power-pole-customization.md`). I prop senza
  queste proprietà restano identici a Maxis.
* **Stili di tralicci** (`[PowerPoles.<nome>]` in `SC4SIM.ini`): modelli per ogni combinazione di direzioni
  (`REG.*`) e distanza tra i tralicci (`InterPoleDistance`, 1–50, Maxis 10). Con lo strumento linee elettriche,
  **Tab** passa allo stile successivo e **Maiusc+Tab** al precedente (solo per i nuovi tralicci); lo stile attivo
  è scritto in `SC4SIM.log`.
* **Linee FAR** (angoli 33,7° / 26,6° / 18,4° / 9,5°): il trascinamento si aggancia da solo agli angoli FAR e usa
  i modelli `FAR-*` dello stile o di `[PowerPoles.FAR]` (`docs/far-power-lines-design.md`). Tenere premuto Maiusc
  per una linea normale.
* **Trucco `keepwires`**: attiva/disattiva il mantenimento delle linee elettriche quando cambia la zona sotto.

Differenze dall'originale: nessun overlay ImGui (lo stato va nel log), impostazioni in `SC4SIM.ini` (sezione
`[SC4PowerPoleCustomization]`, più `DebugLog`), log in `SC4SIM.log`, e il director è un modulo guidato dal director
del SIM. Indirizzi, hook e proprietà sono quelli originali, quindi funziona **solo con SimCity 4 1.1.641**: con
un'altra versione la DLL lo scrive nel log e non installa le patch. Se è caricata anche
`SC4PowerPoleCustomizationSample.dll`, il SIM lascia i tralicci a quella (modificano lo stesso codice del gioco):
installarne una sola.

Codice in `src/powerpoles` (LGPL 2.1, `src/powerpoles/LICENSE-sc4-render-services.txt`), documentazione tecnica in
`docs/`, parser INI `vendor/mINI` (MIT).

## Linee elettriche attraverso zone e campi agricoli (1.3.0)

Aggiunta del SIM, non presente nel codice di caspervg. Nel gioco base `CellIsOKForPlacement` (0x0064FD00) vieta
i tralicci su qualunque casella con una zona, quindi una zona o un campo più largo della distanza tra i tralicci
interrompe la linea. Il SIM intercetta le chiamate 0x0064FF3E (controllo del trascinamento) e 0x006509AD
(`DeterminePolePositions`) e permette i tralicci sulle zone vuote e sui campi agricoli (lotti senza edificio); i
lotti con un edificio bloccano ancora. `CrossZones=false` in `SC4SIM.ini` la disattiva. Analisi completa in
`docs/power-lines-across-zones.md`. Da provare in gioco: con `DebugLog=true` il log scrive ogni casella cambiata.

## Zona rimossa sotto i nuovi tralicci (1.4.0)

`PlacePoles` (0x00651560) gira a ogni movimento del mouse durante il trascinamento, quindi il SIM registra solo le
caselle dei tralicci (chiamate 0x006522C8 e 0x006529CB) e le dezona quando il trascinamento finisce (hook di
`OnMouseUpL`, 0x00661F50, sul controllo di input degli strumenti di rete; `OnMouseDownL` dimentica le caselle di un
trascinamento annullato). Usa la stessa chiamata con cui il gioco dezona (`PlaceZone` con zona None), con il bypass
di keepwires attivo così la linea non viene ridisegnata. I lotti con un edificio non vengono toccati; su un campo
agricolo il gioco demolisce quel campo (non può togliere una sola casella a un lotto) e la fattoria ne fa crescere
di nuovi intorno al traliccio. Impostazioni: `DezoneUnderPoles` e `DezoneFieldCells` (entrambe `true`).

## Occupazione mista R + C (1.5.0)

**Sperimentale: compilata e verificata staticamente, non ancora provata in gioco. Provarla su una copia della
città.**

Nel gioco base Allow Joint Occupancy fa solo dividere **una** capacità tra più tipi di occupanti. Il SIM permette a
un edificio di avere residenti e posti di lavoro commerciali con capacità indipendenti (industria esclusa).

Un edificio è misto se il suo exemplar ha:
- `Allow Joint Occupancy` (0x27812836) = true;
- `Purpose` = Residence, Services o Office (la classe principale);
- un `Capacity Satisfied` con almeno un tipo R (R$, R$$, R$$$), almeno un tipo C (CS$, CS$$, CS$$$, CO$$, CO$$$)
  e nessun tipo I.

Gli altri edifici non cambiano. Esempio: Purpose Residence, Wealth 2, Capacity Satisfied `0x1020, 120, 0x3120, 30`.

Per i lotti misti:
- `GetAvailableCapacity` (slot 52 della vtable del lotto) rende indipendenti R e C; dentro una classe i
  livelli di ricchezza (es. R$ e R$$) restano esclusivi, come in un edificio senza occupazione congiunta;
- `GetOccupancy` (slot 61) conta solo la classe principale;
- ogni mese la popolazione secondaria viene impostata a capacità × desiderabilità (la formula di `UpdatePopulation`
  senza il fattore casuale);
- il censimento della domanda (`PerformCensus`, chiamate 0x006A7475 e 0x006A74AB) conta anche i tipi secondari.

Il trucco `MixedOccupancy` scrive ogni lotto misto in `SC4SIM.log`. Limiti:
- il traffico vede solo il tipo principale;
- la finestra di query mostra solo la capacità principale;
- senza la DLL torna la regola originale.

Analisi completa in `docs/mixed-occupancy.md`. Impostazioni: `[MixedOccupancy] Enabled` e `DebugLog`.

## Advanced Water Management (1.2.0)

Porting nel SC4SIM.dll di [sc4-advanced-water-management](https://github.com/0xC0000054/sc4-advanced-water-management)
di null 45 (licenza MIT, `src/water/LICENSE-sc4-advanced-water-management.txt`).

* **Finestra di query**: il misuratore di efficienza degli edifici idrici mostra l'efficienza reale; una pompa di
  acqua dolce o salata che non produce niente (troppo lontana dall'acqua) mostra 0%.
* **Tooltip al passaggio del mouse**, con [sc4-query-ui-hooks](https://github.com/0xC0000054/sc4-query-ui-hooks):
  lo stesso misuratore e il messaggio di chiusura per inquinamento.
* **Controllo mensile**: una notifica al mese se una pompa di acqua dolce o salata non produce niente (testi in
  `CAM_Locale_en.dat` del SIM).
* **Trucco `ScanPumps`**: lo stesso controllo a richiesta.

Differenze dall'originale: log in `SC4SIM.log`, interruttore `[AdvancedWaterManagement] Enabled` in `SC4SIM.ini`,
hook installato a PostAppInit, niente Windows Implementation Library. Se è caricata anche
`AdvancedWaterManagement.dll`, il SIM lascia la gestione dell'acqua a quella: va tolta dal pacchetto del SIM.

Il director del SIM si iscrive una sola volta ai messaggi (apertura e chiusura città, nuovo mese, trucchi) e li passa
ai moduli.

## Installazione

`SC4SIM.dll` e `SC4SIM.ini` nella cartella Plugins (lo stesso livello). Il file INI è facoltativo.

## Compilare

Visual Studio 2022 o successivo, workload "Sviluppo di applicazioni desktop con C++".
Aprire `SC4SIM.sln`, configurazione **Release | x86** (l'unica), Compila. La DLL è in `bin\Release\SC4SIM.dll`:
va nella cartella Plugins. In alternativa: `cmake -S . -B build -A Win32` e `cmake --build build --config Release`.

Runtime statico: i giocatori non devono installare il Visual C++ Redistributable. Dipendenze nel progetto:
`vendor/gzcom-dll` (LGPL, vedi `vendor/gzcom-dll/SOURCE.txt`), `vendor/mINI` (MIT) e `version.lib` di Windows.
Gli hook dei tralicci usano assembly inline x86 MSVC: serve il compilatore Microsoft (non MinGW o clang).
