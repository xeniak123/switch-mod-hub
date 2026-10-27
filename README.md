# Switch Mod Hub

Homebrew na zmodowanego Nintendo Switcha: **zapisy i mody gier w jednym miejscu**.

- **Mody** – biblioteka modów dla każdej gry, włączanie i wyłączanie jednym przyciskiem (LayeredFS
  Atmosphère: `romfs`, `exefs`, cheaty, łatki `exefs_patches`). Wyłączenie moda przywraca dokładnie
  ten stan, który był wcześniej.
- **Mody online** – przeglądanie i wyszukiwanie modów z tego repozytorium (`mods/index.json`),
  pobieranie z weryfikacją SHA-256, aktualizacje modów.
- **Nakładka (overlay)** dla **Tesla Menu / Ultrahand** – przełączanie modów uruchomionej gry bez wychodzenia z niej.
  Cheaty trafiają do standardowego folderu Atmosphère, więc włączysz je w **EdiZon**.
- **Edytor zapisów** – zapis gry prosto na konsoli (bez JKSV) albo kopie **JKSV**, dla każdej gry:
  zapisy JSON jako drzewo wartości, pozostałe w **edytorze HEX**. Dla **Hollow Knight: Silksong** jest
  pełna „Szybka edycja” (maski, jedwab, narzędzia, herby, relikwie, zadania, dziennik łowcy…).
- **Automatyczne aktualizacje** aplikacji z wydań (Releases) tego repozytorium.
- Interfejs po polsku albo po angielsku, obsługa ekranu dotykowego.

Przeznaczone dla: FW 22.5.0, Atmosphère 1.11.2 (sysMMC / emuMMC). Zbudowane z libnx 4.12.0 (devkitA64 r29.2).

> Mody BepInEx (PC, Mono/.NET) **nie działają** na Switchu – gry Unity na Switchu są kompilowane IL2CPP.
> Na Switchu mody to podmiana plików (LayeredFS), łatki kodu (`exefs`, IPS) i cheaty.

## Instalacja

1. Pobierz `SwitchModHub-vX.Y.Z.zip` z [Releases](https://github.com/xeniak123/switch-mod-hub/releases/latest)
   i rozpakuj na kartę SD (foldery `switch/` łączą się z istniejącymi):
   - `switch/SwitchModHub/SwitchModHub.nro` – aplikacja,
   - `switch/.overlays/SwitchModHub.ovl` – nakładka (wymaga zainstalowanego Tesla Menu albo Ultrahand).
2. Uruchom **Switch Mod Hub** z Homebrew Menu. Do pobierania modów i aktualizacji potrzebny jest internet.
3. Używasz wcześniej *Silksong Save Editor*? Ustawienia i stare kopie bezpieczeństwa są przejmowane automatycznie.

## Ekran główny

- **Gry** – zainstalowane gry (z ikonami), gry z zapisami na konsoli i foldery JKSV. Przy każdej grze: liczba
  zapisów, kopii JKSV i włączonych modów. **A** otwiera grę.
- **Mody online** (**R**) – lista modów z repozytorium dla Twoich gier; **Y** – szukaj (nazwa, gra, autor, tag),
  **X** → *Pokaż mody do wszystkich gier*. Lista jest zapamiętywana i działa bez internetu.

Po otwarciu gry: karta **Zapisy** (zapis w grze, kopie JKSV) i karta **Mody** (L / R).

## Mody

### Jak działa włączanie

- Biblioteka: `sdmc:/switch/SwitchModHub/mods/<ID gry>/<mod>/` (`romfs/`, `exefs/`, `cheats/`, `exefs_patches/`, `mod.json`).
- **Włącz** kopiuje pliki moda do `sdmc:/atmosphere/contents/<ID gry>/…` (łatki do `sdmc:/atmosphere/exefs_patches/…`).
  Każdy plik jest zapisywany w `mods/state.json` razem z sumą CRC32.
- Plik, który już istniał (np. z ręcznie wgranego moda), trafia do `mods/.backup/` i wraca po wyłączeniu moda.
- **Konflikty**: dwa włączone mody nie mogą zmieniać tego samego pliku – aplikacja to wykrywa i nic nie zmienia.
- **Wyłącz** usuwa tylko pliki moda. Plik zmieniony ręcznie po włączeniu moda zostaje na karcie (aplikacja o tym informuje).
- Zmiany działają **po ponownym uruchomieniu gry**.

### Skąd brać mody

- **Mody online** – z tego repozytorium: *Pobierz i włącz* albo *Tylko pobierz*. Pobrany plik musi mieć sumę SHA-256
  z listy, inaczej jest odrzucany. Gdy w repozytorium jest nowsza wersja, mod ma znacznik **AKTUALIZACJA**.
- **Import z karty SD** – skopiuj archiwum `.zip` do `sdmc:/switch/SwitchModHub/import-mods/`, otwórz grę →
  karta **Mody** → **X** → *Importuj mod*. Archiwum może zawierać `romfs/`, `exefs/`, `cheats/`, `exefs_patches/`
  (także w podfolderze) albo układ karty SD (`atmosphere/contents/<ID gry>/…`). Ścieżki typu `../` są odrzucane.

### Nakładka (Tesla Menu / Ultrahand)

W trakcie gry otwórz menu nakładek (domyślnie **L + ↓ + R3** w Tesla Menu, **ZL + ZR + ↓** w Ultrahand) → **Switch Mod Hub**: na górze uruchomiona gra,
niżej wszystkie gry z modami. Przełącznik przy modzie włącza go / wyłącza tym samym silnikiem co aplikacja.

### Cheaty i EdiZon

Mody z folderem `cheats/` instalują pliki do `atmosphere/contents/<ID gry>/cheats/<ID kompilacji>.txt` –
to standardowe miejsce Atmosphère, więc pojedyncze cheaty włączysz w nakładce **EdiZon**.

## Repozytorium modów

Lista modów to [`mods/index.json`](mods/index.json). Jak dodać mod (Pull Request) – opis w [`mods/README.md`](mods/README.md).
Każdy wpis ma m.in. `id`, `name`, `version`, `url` (https, archiwum `.zip`), `sha256` oraz `titleId` (ID gry)
albo `gameMatch` (fragment nazwy gry). Format sprawdza `tools/validate_index.py` (także w GitHub Actions).

## Aktualizacje aplikacji

Przy starcie aplikacja sprawdza najnowsze wydanie w tym repozytorium (można wyłączyć: **X** → *Automatyczne
aktualizacje*; ręcznie: **X** → *Sprawdź aktualizację teraz*). Nowy plik `.nro` jest sprawdzany (rozmiar, nagłówek NRO,
SHA-256 z GitHuba), zapisywany obok starego i podmieniany dopiero po weryfikacji; potem aplikacja uruchamia się ponownie.
Nakładkę aktualizuje się, kopiując nowy `SwitchModHub.ovl` z paczki.

## Edytor zapisów

### Zapis prosto w grze (bez JKSV)

1. **Zamknij grę** (HOME → X), jeśli działa w tle – inaczej aplikacja odmówi zapisu.
2. Otwórz grę → karta **Zapisy** → **Zapis w grze – <użytkownik>** → wybierz plik.
3. Edytuj, potem **+** → **Zapisz w grze**. Przed zapisem cały obecny zapis gry trafia do
   `sdmc:/switch/SwitchModHub/backups/` (format JKSV z `.nx_save_meta.bin`).

### Kopie JKSV

Wybierz kopię i plik, edytuj, **+** → *Zapisz jako NOWĄ kopię JKSV* (zalecane), *Nadpisz* (z kopią bezpieczeństwa)
albo *Wgraj do gry*. Kopie JKSV są przypisywane do gier po nazwie folderu albo po ID gry (*Use Title IDs*).

### Karty edytora

- **Szybka edycja** (Silksong) – najważniejsze wartości, sloty herbów, relikwie, zadania, dziennik łowcy,
  tryb Steel Soul; ostrzeżenia o limitach gry.
- **Wszystkie dane** – pełne drzewo każdego zapisu JSON (każda gra): liczby, tak/nie, teksty, duplikowanie
  i usuwanie elementów list, wyszukiwanie (**Y**).
- **HEX** – zapisy, które nie są JSON-em: bajty, liczby 8/16/32/64-bit (LE), float, tekst; **Y** – szukaj
  (bajty `DE AD` albo tekst w cudzysłowie), **X** – przywróć bajt.
- **Akcje** (Silksong) – operacje zbiorcze, import / eksport zapisu PC.
- **Zmiany** – lista wszystkich zmian („było → jest”); **X** cofa zmianę.

Opcjonalny `sdmc:/switch/SwitchModHub/definitions.json` dopisuje elementy Silksonga z DLC
(przykład: `extras/definitions.example.json`).

## Sterowanie

| Przycisk | Działanie |
|---|---|
| Góra / Dół | wybór (przytrzymaj = szybkie przewijanie) |
| Lewo / Prawo | zmiana wartości / przełącznik; w HEX: kursor |
| A | otwórz / edytuj / wybierz |
| B | wstecz (czyści też wyszukiwanie) |
| X | Menu (listy), menu elementu / przywróć (edytor) |
| Y | odśwież (listy), szukaj (Mody online, drzewo, HEX), następna sekcja (Szybka edycja) |
| L / R | poprzednia / następna karta |
| ZL / ZR | strona w górę / w dół |
| + | zapisz (edytor), wyjście (listy) |
| − | pomoc |
| Dotyk | stuknięcie = wybór, drugie stuknięcie = otwórz, przeciągnięcie = przewijanie |

## Bezpieczeństwo danych

- Zapis w grze zmienia się dopiero po zapisie, weryfikacji i zatwierdzeniu (commit); każdy błąd zostawia
  zapis gry bez zmian. Kopię bezpieczeństwa wgrasz z powrotem: lista gier → *Kopie bezpieczeństwa edytora*.
- Każdy zapis jest weryfikowany (ponowny odczyt, CRC archiwów ZIP, porównanie bajt po bajcie).
- Mody: rejestr plików z CRC32, kopie zastępowanych plików, wykrywanie konfliktów, odrzucanie niebezpiecznych ścieżek.
- Log: `sdmc:/switch/SwitchModHub/log.txt`.

## Budowanie

Wymaga devkitPro (devkitA64 + libnx). Najprościej przez oficjalny obraz Docker:

```bash
docker run --rm -v "$PWD:/project" -w /project devkitpro/devkita64 sh -c "make && make -C overlay"
```

Wynik: `SwitchModHub.nro` i `overlay/SwitchModHub.ovl`. Każdy push buduje oba pliki w GitHub Actions,
a tag `v*` publikuje wydanie (Release) z paczką `.zip` i sumami SHA-256.

Testy na PC (Linux / WSL: g++, python3, openssl, unzip, czcionka TTF w `SSE_FONTS`):

```bash
bash tests/run_tests.sh
```

Kompiluje aplikację z AddressSanitizer/UBSan, uruchamia testy jednostkowe (SHA-256, AES, JSON, silnik modów,
indeks repozytorium, aktualizacje) i skryptowe scenariusze interfejsu na symulowanej konsoli (zapisy gier,
zainstalowane gry, internet z repozytorium modów i wydaniem aplikacji), a wyniki sprawdza niezależnymi narzędziami.

**Uwaga:** dostęp do zapisów gier, sieć (HTTPS przez libcurl + usługę SSL konsoli), lista gier i nakładka używają
standardowych funkcji libnx / libtesla, ale testy na PC je symulują – na prawdziwym Switchu te części nie były
jeszcze uruchamiane. Zgłaszaj problemy w [Issues](https://github.com/xeniak123/switch-mod-hub/issues) (dołącz `log.txt`).

## Licencja

GPL-2.0 (plik `LICENSE`) – nakładka używa [libtesla](https://github.com/WerWolv/libtesla) (GPL-2.0).
Inne biblioteki: [stb_truetype, stb_image](https://github.com/nothings/stb) (public domain / MIT),
[miniz](https://github.com/richgel999/miniz) (MIT, `source/third_party/miniz_LICENSE.txt`),
libcurl i zlib z devkitPro (portlibs). Czcionki: systemowe czcionki Switcha.
Nazwy przedmiotów Silksonga pochodzą z angielskich tekstów gry. Projekt nie jest powiązany z Nintendo ani z twórcami gier.
