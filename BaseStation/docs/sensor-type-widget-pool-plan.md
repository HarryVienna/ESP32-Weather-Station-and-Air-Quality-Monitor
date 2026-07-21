# Plan: Sensor-Typ pro Slot frei waehlbar (Widget-Pool)

## Problem

Aktuell ist der Sensor-Typ (bme280/sht45/geiger/...) pro Slot fest im EEZ-Studio-Layout
verdrahtet: jeder der 6 `Sensor_X`-Container im Weatherstation-Screen hat dauerhaft einen
bestimmten Widget-Typ, siehe `sensor_slots[]` in [gui.c](../main/gui/gui.c) (Abschnitt "b)
Hardware-Zuordnung"). Gewuenscht: der Sensor-Typ soll im Setup Screen pro Slot **frei
waehlbar** sein, und der jeweils passende, in EEZ Studio gestaltete Widget-Typ soll dann im
Slot erscheinen - ohne dass jeder der 6 Container alle moeglichen Typen verschachtelt enthaelt
("umstaendliche Loesung").

## Wie EEZ Studios Codegen tatsaechlich funktioniert (Rechercheergebnis)

- Jede **Platzierung** eines "User Widget" in EEZ Studio erzeugt einen Aufruf
  `create_user_widget_<name>(lv_obj_t *parent_obj, int startWidgetIndex)` in
  [screens.c](../main/ui/screens.c). Mehrere Platzierungen desselben Widget-Typs auf einer
  Seite erzeugen mehrere Aufrufe **derselben Funktion** mit unterschiedlichem
  `startWidgetIndex` - siehe `create_user_widget_sensor_sht45(obj, 88/99/110/121)` fuer die
  Slots 1-4.
- `startWidgetIndex` ist einfach ein Parameter - die Funktion schreibt intern
  `((lv_obj_t **)&objects)[startWidgetIndex + N] = obj;` fuer jedes benannte Kind-Widget. Der
  Zielspeicher ist aber **immer** `&objects` (das generierte, feste `screens.h`/`screens.c`
  Struct) - wir koennen also **keine eigenen** Indizes erfinden, sondern nur die von EEZ Studio
  beim Platzieren reservierten Bereiche wiederverwenden.
- Folge: um zur Laufzeit **jede Kombination** aus 6 Slots x N Sensortypen abzudecken (Worst
  Case: alle 6 Slots gleichzeitig derselbe Typ, z.B. 6x Geiger), muessen in EEZ Studio
  vorab **bis zu 6 Platzierungen pro Typ** existieren. Das ist eine strukturelle Grenze von
  EEZ Studios statischem Codegen, unabhaengig davon, wo/wie diese Platzierungen im Layout
  liegen.
- Bereits vorhanden, aber ungenutzt (laut Nutzer: angefangene, nie fertiggestellte Vorarbeit
  fuer genau dieses Feature): `create_user_widget_sensor_kohlenmonoxid`,
  `create_user_widget_sensor_bme280_1..4`, `create_user_widget_sen66_widget_2`,
  `create_user_widget_base_1/2/3/6` in screens.c/.h - aktuell keine einzige Aufrufstelle in
  create_screen_weatherstation_screen(). Diese Instanzen (bzw. ihre Zaehlung pro Typ) muessen
  vor der Umsetzung geprueft/ergaenzt werden: fuer volle Flexibilitaet braucht jeder waehlbare
  Sensortyp 6 Platzierungen, nicht nur 1-5.

## Loesungsidee: Widget-Pool statt verschachtelter Container

Statt pro Slot-Container alle Typen zu verschachteln (viele geschachtelte, meist unsichtbare
Widgets + Show/Hide-Logik pro Container), werden alle Typ-Instanzen **flach in einem
unsichtbaren Pool-Bereich** gehalten und zur Laufzeit in den passenden Slot **umgehaengt**
(reparented). Das ist eine Verallgemeinerung des bereits existierenden Chart-Platzhalter-Musters
(`init_charts()` in [gui.c](../main/gui/gui.c), Abschnitt 6): dort wird ein leerer EEZ-Platzhalter
einmalig durch ein echtes Widget ersetzt. Hier: ein leerer EEZ-Platzhalter bekommt zur Laufzeit
eines von mehreren vorgefertigten EEZ-Widgets als Kind zugewiesen, je nach Konfiguration -
und das kann sich (im Gegensatz zu den Charts) bei jedem "Starten"-Klick im Setup Screen
aendern.

### 1. EEZ-Studio-Seite (vom Nutzer zu bauen, ich habe keinen Zugriff auf den Designer)

- Jeder der 6 `Sensor_X`-Container im Weatherstation-Screen wird zu einem **leeren
  Platzhalter-Container** (Groesse/Position wie heute, aber ohne festen Inhalt) - analog zu
  `objects.hourly_chart`/`objects.daily_chart` heute.
- Ein neuer, nicht sichtbarer "Pool"-Bereich (z.B. eigene Flaeche weit ausserhalb des
  sichtbaren Screens, oder ein eigener, nie angezeigter Screen) enthaelt fuer **jeden
  waehlbaren Sensortyp 6 Platzierungen** des jeweiligen User Widgets:
  - bme280 x6
  - sht45 x6
  - geiger x6
  - kohlenmonoxid/CO2 x6
  - (weitere Typen nach Bedarf, z.B. ein zweiter sen66-Kartentyp)
- Jede Platzierung erzeugt (siehe oben) einen Aufruf mit eigenem `startWidgetIndex` in
  screens.c. Diese Aufrufe muessen NICHT von Hand aufgeraeumt werden - nur die Anzahl der
  Platzierungen pro Typ zaehlt (6 Stueck).
- Export nach C wie gewohnt (`screens.c`/`screens.h` werden komplett neu generiert - das ist
  ok, siehe Kommentar in gui.c: "ein erneuter EEZ-Studio-Export verliert diesen Schritt nie").

### 2. C-Seite: generische Pool-Verwaltung (in gui.c, sobald EEZ-Seite fertig ist)

Ersetzt den aktuellen statischen `sensor_slots[]`-Mechanismus durch:

```c
typedef struct {
  lv_obj_t *name, *icon, *battery, *wifi, *value1, *value2, *value3, *header;
} sensor_widget_t;
```

- **Pool-Init** (einmalig nach `create_screens()`, analog `init_charts()`): fuer jeden
  Sensortyp alle 6 vorbereiteten Instanzen in einen unsichtbaren Sammel-Parent erzeugen
  (`create_user_widget_sensor_bme280(pool_parent, IDX_BME280_0)` usw.) und deren Kind-Zeiger
  generisch in `sensor_widget_t pool[TYPE][6]` ablegen (kein Zugriff mehr auf einzelne
  `objects.objN__xxx`-Felder ausserhalb dieser Init-Funktion noetig).
- **`apply_slot_configs()`** (bisher: nur Name/Icon uebertragen) wird erweitert: fuer jeden der
  6 Slots wird anhand des im Setup Screen gewaehlten Typs eine freie Pool-Instanz dieses Typs
  ausgewaehlt, per `lv_obj_set_parent()` + Groesse/Position in den zugehoerigen
  Platzhalter-Container gehaengt, und ein Zeiger darauf in einer **laufzeit-gefuellten**
  Variante der heutigen `sensor_slots[]`-Tabelle abgelegt (`sensor_widget_t
  *active_slot[SENSOR_SLOT_COUNT]`).
- `disp_sensor_values()`, `disp_sensor_link_quality()`, `disp_sensor_offline()` aendern sich
  kaum: sie lesen statt der `static const sensor_slots[]`-Tabelle die laufzeit-gefuellte
  `active_slot[]`-Tabelle (gleiche Feldnamen/Struktur wie heute, nur generisch statt
  `objN__xxx`-spezifisch).
- Neues Setup-Screen-UI-Element: pro Slot ein zusaetzliches Dropdown "Sensortyp" (bme280/
  sht45/geiger/...) neben dem bestehenden Icon/Name-Dropdown, Auswahl wird wie `sensor%d_icon`
  in NVS gespeichert (z.B. `sensor%d_type`) und beim Start geladen.
- Nicht verwendete Pool-Instanzen bleiben unsichtbar im Pool-Parent haengen (kein
  `lv_obj_del`, da beim naechsten "Starten"-Klick mit anderer Typ-Wahl wieder gebraucht) -
  einfach `lv_obj_add_flag(LV_OBJ_FLAG_HIDDEN)` waehrend sie im Pool sind bzw. beim
  Zurueckhaengen in den Pool.

### 3. Reihenfolge der Umsetzung

1. EEZ Studio: Platzhalter-Container fuer die 6 Slots anlegen, Pool-Bereich mit 6x pro Typ
   bauen, exportieren.
2. Pruefen, welche `startWidgetIndex`-Konstanten/Funktionsnamen dabei fuer jeden Pool-Slot
   entstehen (das entscheidet die konkrete Init-Tabelle im naechsten Schritt).
3. C-Code in gui.c wie oben beschrieben implementieren.
4. Setup-Screen-Dropdown fuer Sensortyp ergaenzen + NVS-Persistenz.
5. Build + Test auf echter Hardware (Sensor-Typ im Setup wechseln, pruefen dass richtiges
   Widget im Slot erscheint, alte Werte-Updates weiter funktionieren).

## Offene Fragen fuer spaeter

- Reicht ein "unsichtbarer Bereich weit ausserhalb des Screens" als Pool-Parent, oder soll es
  ein eigener, nie geladener EEZ-Studio-Screen sein (sauberer, aber `create_screen_*()` muss
  dann manuell statt ueber Navigation aufgerufen werden)?
- Werden wirklich alle Typen mit voller 6x-Flexibilitaet gebraucht, oder reicht es, den Pool
  nur für die Typen zu vergroessern, bei denen mehrere Sensoren gleichzeitig realistisch sind
  (spart EEZ-Studio-Fleissarbeit und Flash/RAM fuer ungenutzte Pool-Instanzen)?
