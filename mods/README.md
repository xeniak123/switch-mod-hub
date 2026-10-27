# Repozytorium modów Switch Mod Hub

Aplikacja czyta listę modów z
`https://raw.githubusercontent.com/xeniak123/switch-mod-hub/main/mods/index.json`.
Każda zmiana w `main` jest od razu widoczna w aplikacji (karta **Mody online**, **Y** odświeża).

## Jak dodać mod

1. Przygotuj archiwum `.zip` w jednym z układów:
   - `romfs/…`, `exefs/…`, `cheats/<ID kompilacji>.txt`, `exefs_patches/<nazwa>/<ID>.ips` (mogą być w podfolderze),
   - albo układ karty SD: `atmosphere/contents/<ID gry>/romfs/…` itd.
   Inne pliki (readme, zrzuty ekranu) są pomijane przy instalacji. Opcjonalny `mod.json` w archiwum
   (`name`, `version`, `author`, `description`, `tags`) uzupełnia brakujące dane.
2. Wgraj archiwum do `mods/files/` (małe pliki) albo jako załącznik wydania (Release) – liczy się trwały adres `https://`.
3. Dopisz wpis w `mods/index.json`:

```json
{
  "id": "moj-mod",
  "name": "Mój mod",
  "version": "1.0.0",
  "author": "nick",
  "game": "Nazwa gry",
  "titleId": "0100XXXXXXXXXXXX",
  "type": "romfs",
  "description": "Co zmienia mod.",
  "tags": ["grafika"],
  "gameVersion": "1.0.2",
  "url": "https://raw.githubusercontent.com/xeniak123/switch-mod-hub/main/mods/files/moj-mod-1.0.0.zip",
  "sha256": "64 znaki szesnastkowe – sha256sum moj-mod-1.0.0.zip",
  "size": 12345
}
```

   - `id` – małe litery, cyfry, `-`, `_`, `.`; unikalny w ramach gry (to nazwa folderu w bibliotece na karcie SD).
   - `titleId` (16 znaków, ID gry) **albo** `gameMatch` – fragment nazwy gry (np. `"Silksong"`), gdy ID nie jest znane.
   - `version` – nowsza wersja (np. `1.0.1` > `1.0.0`) pokazuje w aplikacji znacznik **AKTUALIZACJA**.
   - `sha256` i `size` muszą zgadzać się z plikiem – aplikacja odrzuca pobrany plik z inną sumą.
4. Uruchom `python3 tools/validate_index.py` i otwórz Pull Request. GitHub Actions sprawdza listę automatycznie.

## Zasady

- Tylko mody, które wolno rozpowszechniać (własne albo za zgodą autora; podaj autora).
- Bez plików gry objętych prawem autorskim w całości, bez piractwa, bez złośliwego kodu.
- Łatki `exefs` / cheaty działają tylko z konkretną wersją gry – wpisz ją w `gameVersion` i w opisie.
