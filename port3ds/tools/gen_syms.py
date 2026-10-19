#!/usr/bin/env python3
"""
gen_syms.py - erzeugt syms.h aus pokered.sym.

Der untere Bildschirm liest Spielstand (WRAM) und Grafiken/Tabellen (ROM)
direkt ueber die Symboladressen der Decomp. Aendert sich das Layout der
ROM (eigene Aenderungen am Hack), passt sich der Port beim naechsten
Build automatisch an.
"""
import re
import sys

WANTED = [
    # ROM
    'FontGraphics', 'MonsterSprite', 'PokeBallSprite', 'FairySprite', 'BirdSprite',
    'SeelSprite', 'BugIconFrame1', 'BugIconFrame2', 'PlantIconFrame1', 'PlantIconFrame2',
    'SnakeIconFrame1', 'SnakeIconFrame2', 'QuadrupedIconFrame1', 'QuadrupedIconFrame2',
    'PokedexOrder', 'MonPartyData', 'CompressedMap', 'WorldMapTileGraphics',
    'ExternalMapEntries', 'InternalMapEntries', 'MonsterNames', 'RedSprite',
    'BadgeNumbersTileGraphics', 'TextBoxGraphics',
    # WRAM
    'wPartyCount', 'wPartySpecies', 'wPartyMon1', 'wPartyMon2', 'wPartyMon1HP',
    'wPartyMon1Status', 'wPartyMon1Level', 'wPartyMon1MaxHP', 'wPartyMonNicks',
    'wCurMap', 'wPlayerName', 'wObtainedBadges', 'wPlayerMoney', 'wIsInBattle',
    'wPlayTimeHours', 'wPlayTimeMinutes', 'wPokedexOwned', 'wPlayerMonNumber',
    'wBattleMonHP', 'wBattleMonStatus', 'wXCoord', 'wYCoord', 'wLastMap',
    # Kampf-Menues auf dem unteren Bildschirm
    'wCurrentMenuItem', 'wTopMenuItemX', 'wTopMenuItemY', 'wMaxMenuItem', 'wListScrollOffset', 'wListMenuID',
    'wBattleMonMoves', 'wBattleMonPP', 'wBattleMonSpecies', 'wBattleMonLevel',
    'wBattleMonMaxHP', 'wBattleMonNick', 'wEnemyMonNick', 'wEnemyMonLevel',
    'wNumBagItems', 'wBagItems', 'wBattleType', 'wMoveMenuType', 'wWhichPokemon',
    'wTileMapBackup', 'Moves', 'MoveNames', 'TypeNames', 'ItemNames',
    # aktive ROM-Bank (Pruefung der Ruecksprungadressen auf dem Stapel)
    'hLoadedROMBank', 'wVBlankSavedROMBank', 'Bankswitch.Return', 'Predef.done',
]
OPTIONAL = {'BadgeNumbersTileGraphics', 'TextBoxGraphics', 'RedSprite'}

# Codebereiche (Start-Label, End-Label). Steht eine Ruecksprungadresse aus
# diesem Bereich auf dem Game-Boy-Stapel, wartet das Spiel gerade in dieser
# Funktion auf eine Eingabe. Daran erkennt die UI das offene Kampfmenue.
RANGES = [
    ('DisplayBattleMenu', 'BagWasSelected'),
    ('SelectMenuItem', 'SelectMenuItem_CursorUp'),
    ('DisplayListMenuIDLoop', 'DisplayChooseQuantityMenu'),
    ('HandlePartyMenuInput', 'DrawPartyMenu'),
    ('StatusScreen', 'StatusScreenExpText'),
    ('PartyMenuOrRockOrRun.partyMonWasSelected', 'PartyMenuOrRockOrRun.switchMon'),
    # Spielzustand fuer den unteren Bildschirm: Overworld = im Spiel,
    # Titelbild/Hauptmenue = noch nicht im Spiel (unten schwarz)
    ('EnterMap', 'NewBattle'),
    ('DisplayTitleScreen', 'TitleScreenPickNewMon'),
    ('MainMenu', 'InitOptions'),
]


def main():
    if len(sys.argv) != 3:
        print('Aufruf: gen_syms.py pokered.sym syms.h')
        return 1
    syms = {}
    for line in open(sys.argv[1], encoding='utf-8'):
        m = re.match(r'([0-9a-fA-F]+):([0-9a-fA-F]+)\s+(\S+)', line)
        if m:
            syms.setdefault(m.group(3), (int(m.group(1), 16), int(m.group(2), 16)))
    out = ['/* Automatisch erzeugt von tools/gen_syms.py - nicht bearbeiten. */',
           '#ifndef POKERED_SYMS_H', '#define POKERED_SYMS_H', '']
    missing = []
    for name in WANTED:
        if name not in syms:
            if name in OPTIONAL:
                continue
            missing.append(name)
            continue
        bank, addr = syms[name]
        cname = name.replace('.', '_')
        out.append('#define SYM_%s 0x%04x' % (cname, addr))
        out.append('#define BANK_%s 0x%02x' % (cname, bank))
    for start, end in RANGES:
        if start not in syms or end not in syms:
            missing.append(start + '..' + end)
            continue
        cname = start.replace('.', '_')
        bank, addr = syms[start]
        out.append('#define SYM_%s 0x%04x' % (cname, addr))
        out.append('#define BANK_%s 0x%02x' % (cname, bank))
        out.append('#define SYM_%s_END 0x%04x' % (cname, syms[end][1]))
    out += ['', '#endif', '']
    if missing:
        print('Fehlende Symbole: ' + ', '.join(missing), file=sys.stderr)
        return 1
    with open(sys.argv[2], 'w') as f:
        f.write('\n'.join(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
