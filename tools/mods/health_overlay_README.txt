SILKSONG: HP + OBRAZENIA — 0.1.0 EKSPERYMENTALNY

Status: zbudowany i sprawdzony statycznie oraz w emulacji ARM64 ze sztucznymi
wywolaniami silnika. NIE zostal przetestowany w uruchomionej grze na Switchu.
Wymaga testu uruchomienia, walki, zmiany scen i bossow wielofazowych.

Wymagana gra: Nintendo Switch, Hollow Knight: Silksong, baza v0 BEZ aktualizacji.
Title ID: 010013C00E930000
Build ID: 9049EA1C527A35D70F1ECE5E784C62E000000000

Instalacja w Switch Mod Hub:
1. Skopiuj ZIP do sdmc:/switch/SwitchModHub/import-mods/.
2. Silksong -> Mody -> X -> Importuj mod, wybierz ZIP, potem Wlacz.
3. Calkowicie zamknij gre i uruchom ja ponownie.
Usuwanie: Wylacz mod w Hubie, potem ponownie uruchom gre.
Nie zmienia zapisow gry; nie zawiera plikow gry ani DLL BepInEx.

Dzialanie:
- Po trafieniu zwykly przeciwnik dostaje pasek HP i liczby aktualne/maksymalne HP.
- Pasek zwyklego przeciwnika znika po 3 sekundach bez trafienia.
- Poczatkowe HP > 105 wybiera duzy dolny pasek (do 3 widocznych naraz).
  To prog HP, a nie rozpoznawanie tozsamosci bossa.
- Liczba obrazen to spadek HP po trafieniu gracza; unosi sie i zanika przez 1 s.
- Sledzi aktywnych przeciwnikow, nie przechowuje wskaznikow do usunietych wrogow.
- Skaluje interfejs wzgledem wysokosci 720p; limit 24 paskow na klatke.

Ograniczenia tej wersji:
- Nie jest pelnym portem ShowDamage HealthBar. Brak konfiguratora, tekstur,
  nazw bossow, zmiany kolorow i niezaleznych przelacznikow.
- Liczby znikaja razem z usunietym przeciwnikiem, takze przy ostatnim ciosie.
- Maksymalne HP pochodzi z initHp gry; fazy i nietypowe mechaniki bossow
  wymagaja testow. Wartosc ponad 99999 nie jest obslugiwana.
- Monitoruje HealthManager.TakeDamage, nie gwarantuje wszystkich dodatkowych
  sciezek obrazen (np. odrebnych efektow statusowych).
- Inne latki zmieniajace te same instrukcje albo koncowke strony kodu koliduja.
  Hub wykrywa konflikt plikow, nie nakladanie adresow wewnatrz roznych IPS.

Autor wydania: xeniak123. Inspiracja funkcjonalna: ShowDamage HealthBar,
Xiaohai / XiaoHaiiOvO: https://www.nexusmods.com/hollowknightsilksong/mods/28
Kod napisany od nowa dla Switcha; bez kodu i grafik moda na PC.
Kod zrodlowy i budowanie: tools/mods/health_overlay_BUILD.txt w repozytorium.
