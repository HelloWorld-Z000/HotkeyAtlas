# Regenerates the English translation template from src/main.cpp and Notes.json: every
# TL("..."), TLF("...") and N_("...") literal and every English note becomes a line
# "English = English".
# Translators copy the file to <language>.txt (e.g. russian.txt) and change the right side.

$root  = Split-Path -Parent $PSScriptRoot
$src   = Get-Content -Raw -Encoding UTF8 (Join-Path $root 'src\main.cpp')
$notes = Join-Path $root 'dist\SKSE\Plugins\HotkeyAtlas\Notes.json'
$out   = Join-Path $root 'dist\SKSE\Plugins\HotkeyAtlas\Translations\english.txt'

$seen = [ordered]@{}
function Add-Text([string]$text) {
    if ($text -eq '' -or $seen.Contains($text)) { return }
    if ($text.Contains('=')) { throw "text contains '=', which the translation format cannot hold: $text" }
    $seen[$text] = $true
}

$regex = [regex]'\b(?:TL|TLF|N_)\("((?:[^"\\]|\\.)*)"'
foreach ($m in $regex.Matches($src)) {
    Add-Text ($m.Groups[1].Value -replace '\\"', '"')   # keep \n escapes: the plugin reads them back
}

# Notes.json: a note is "text" or { "english": "text", ... } (those with their own translations
# are listed too, other languages may still want them)
function Add-Note($value) {
    if ($value -is [string]) { Add-Text ($value -replace "`n", '\n') }
    elseif ($value -and $value.english) { Add-Text ($value.english -replace "`n", '\n') }
}
$json = Get-Content -Raw -Encoding UTF8 $notes | ConvertFrom-Json
foreach ($section in 'controls', 'devices') {
    foreach ($group in $json.$section.PSObject.Properties) {
        foreach ($entry in $group.Value.PSObject.Properties) { Add-Note $entry.Value }
    }
}
foreach ($entry in $json.directions.PSObject.Properties) { Add-Note $entry.Value }
foreach ($entry in $json.quickSlots.PSObject.Properties) { Add-Note $entry.Value }
foreach ($mod in $json.mods) { Add-Note $mod.text }

$lines = @(
    '; Hotkey Atlas translation: English (the built-in text; this file is the template).'
    ';'
    '; To translate: copy this file to <language>.txt in the same folder, e.g. russian.txt,'
    '; and change the text right of "=". Keep the left side exactly as it is.'
    '; The file name is the language: Hotkey Atlas picks the one matching the game''s'
    '; sLanguage (english, russian, german, french, ...) or the one chosen with the Language'
    '; button. Save as UTF-8. {0}, {1} are filled in by the plugin and must stay.'
    '; \n is a line break. Lines left out or with an empty right side stay English.'
    ''
)
$lines += $seen.Keys | ForEach-Object { "$_ = $_" }

New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
[IO.File]::WriteAllLines($out, [string[]]$lines, (New-Object Text.UTF8Encoding $false))
"wrote $($seen.Count) strings to $out"
