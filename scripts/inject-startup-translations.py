#!/usr/bin/env python3
"""
One-off helper: inject translations for the two CriticalStartupError
messages introduced in the "fail-fast startup" refactor.

Use case: a small set of msgids that msgmerge has left as empty msgstr
(because they are brand new) and that don't warrant firing up a full
translation workflow for ten languages. Run once, commit the .po diff,
delete the script — it's a scaffold, not a permanent tool.

Safe to re-run: idempotent, only touches msgstr entries that are empty.
"""

import os
import re
import sys

CONFIG_MSGID = (
    "Could not load configuration from {}. "
    "Re-install the application or restore the config file."
)
DB_MSGID = (
    "SQLite initialisation failed. Check logs for details and verify "
    "disk space / permissions for the database path."
)
BADGE_LIGHT_ONLY = "Light only"
BADGE_DARK_ONLY = "Dark only"
BADGE_DARK_LIGHT = "Dark + Light"

TRANSLATIONS = {
    "de": {
        "config": "Konfiguration konnte nicht geladen werden aus {}. "
                  "Installieren Sie die Anwendung neu oder stellen Sie "
                  "die Konfigurationsdatei wieder her.",
        "db":     "SQLite-Initialisierung fehlgeschlagen. Prüfen Sie die Logs "
                  "und verifizieren Sie Speicherplatz / Berechtigungen für "
                  "den Datenbankpfad.",
        "light_only": "Nur Hell",
        "dark_only":  "Nur Dunkel",
        "dark_light": "Dunkel + Hell",
    },
    "es": {
        "config": "No se pudo cargar la configuración desde {}. "
                  "Reinstale la aplicación o restaure el archivo de "
                  "configuración.",
        "db":     "La inicialización de SQLite falló. Consulte los logs y "
                  "verifique espacio en disco / permisos de la ruta de la "
                  "base de datos.",
        "light_only": "Solo claro",
        "dark_only":  "Solo oscuro",
        "dark_light": "Oscuro + Claro",
    },
    "es_MX": {
        "config": "No se pudo cargar la configuración desde {}. "
                  "Reinstale la aplicación o restaure el archivo de "
                  "configuración.",
        "db":     "La inicialización de SQLite falló. Consulte los logs y "
                  "verifique espacio en disco / permisos de la ruta de la "
                  "base de datos.",
        "light_only": "Solo claro",
        "dark_only":  "Solo oscuro",
        "dark_light": "Oscuro + Claro",
    },
    "fr": {
        "config": "Impossible de charger la configuration depuis {}. "
                  "Réinstallez l'application ou restaurez le fichier de "
                  "configuration.",
        "db":     "L'initialisation de SQLite a échoué. Consultez les logs "
                  "et vérifiez l'espace disque / les permissions pour le "
                  "chemin de la base de données.",
        "light_only": "Clair uniquement",
        "dark_only":  "Sombre uniquement",
        "dark_light": "Sombre + Clair",
    },
    "it": {
        "config": "Impossibile caricare la configurazione da {}. "
                  "Reinstallare l'applicazione o ripristinare il file di "
                  "configurazione.",
        "db":     "Inizializzazione SQLite fallita. Controlla i log e "
                  "verifica spazio su disco / permessi per il percorso "
                  "del database.",
        "light_only": "Solo chiaro",
        "dark_only":  "Solo scuro",
        "dark_light": "Scuro + Chiaro",
    },
    "pt": {
        "config": "Não foi possível carregar a configuração de {}. "
                  "Reinstale a aplicação ou restaure o ficheiro de "
                  "configuração.",
        "db":     "Falha na inicialização do SQLite. Consulte os logs e "
                  "verifique espaço em disco / permissões para o caminho "
                  "da base de dados.",
        "light_only": "Apenas claro",
        "dark_only":  "Apenas escuro",
        "dark_light": "Escuro + Claro",
    },
    "pt_BR": {
        "config": "Não foi possível carregar a configuração de {}. "
                  "Reinstale o aplicativo ou restaure o arquivo de "
                  "configuração.",
        "db":     "Falha na inicialização do SQLite. Verifique os logs e o "
                  "espaço em disco / permissões para o caminho do banco de "
                  "dados.",
        "light_only": "Apenas claro",
        "dark_only":  "Apenas escuro",
        "dark_light": "Escuro + Claro",
    },
    "fi": {
        "config": "Kokoonpanon lataus epäonnistui: {}. Asenna sovellus "
                  "uudelleen tai palauta kokoonpanotiedosto.",
        "db":     "SQLiten alustus epäonnistui. Tarkista lokit ja varmista "
                  "levytila / oikeudet tietokantapolulle.",
        "light_only": "Vain vaalea",
        "dark_only":  "Vain tumma",
        "dark_light": "Tumma + Vaalea",
    },
    "sv": {
        "config": "Kunde inte läsa konfigurationen från {}. Installera om "
                  "applikationen eller återställ konfigurationsfilen.",
        "db":     "SQLite-initiering misslyckades. Kontrollera loggarna och "
                  "verifiera diskutrymme / behörigheter för "
                  "databassökvägen.",
        "light_only": "Endast ljust",
        "dark_only":  "Endast mörkt",
        "dark_light": "Mörkt + Ljust",
    },
    "ga": {
        "config": "Níorbh fhéidir an chumraíocht a lódáil ó {}. "
                  "Athshuiteáil an feidhmchlár nó cuir an comhad "
                  "cumraíochta ar ais.",
        "db":     "Theip ar thúsú SQLite. Seiceáil na lomaí agus fíoraigh "
                  "an spás diosca / na ceadanna don chosán bunachair "
                  "shonraí.",
        "light_only": "Solas amháin",
        "dark_only":  "Dorcha amháin",
        "dark_light": "Dorcha + Solas",
    },
}


def po_escape(s: str) -> str:
    """Escape a string for inclusion in a .po msgstr literal."""
    # Order matters: backslashes first, then quotes, then newlines.
    s = s.replace(chr(92), chr(92) + chr(92))
    s = s.replace('"', chr(92) + '"')
    s = s.replace("\n", chr(92) + "n")
    return '"' + s + '"'


def unquote_po(quoted: str) -> str:
    """Inverse of po_escape for a single "..." literal."""
    inner = quoted[1:-1]
    out = []
    i = 0
    while i < len(inner):
        c = inner[i]
        if c == chr(92) and i + 1 < len(inner):
            nxt = inner[i + 1]
            out.append({"n": "\n", '"': '"', chr(92): chr(92)}.get(nxt, nxt))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def replace_msgstr(content: str, target_msgid: str, translation: str) -> str:
    """Replace the msgstr block that follows the given msgid."""
    lines = content.split("\n")
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith('msgid "'):
            # Collect possibly-multi-line msgid.
            msgid_text = ""
            m = re.match(r'msgid (".*")', line)
            if m:
                msgid_text += unquote_po(m.group(1))
            out.append(line)
            j = i + 1
            while j < len(lines) and lines[j].startswith('"'):
                m = re.match(r'(".*")', lines[j])
                if m:
                    msgid_text += unquote_po(m.group(1))
                out.append(lines[j])
                j += 1
            # j now indexes the msgstr line.
            if msgid_text == target_msgid \
                    and j < len(lines) and lines[j].startswith("msgstr"):
                out.append("msgstr " + po_escape(translation))
                j += 1
                while j < len(lines) and lines[j].startswith('"'):
                    j += 1
            else:
                while j < len(lines) and (lines[j].startswith("msgstr")
                                          or lines[j].startswith('"')):
                    out.append(lines[j])
                    j += 1
            i = j
            continue
        out.append(line)
        i += 1
    return "\n".join(out)


def main(po_dir: str) -> int:
    for lang, pairs in TRANSLATIONS.items():
        path = os.path.join(po_dir, f"{lang}.po")
        if not os.path.exists(path):
            print(f"skip: {path} not found", file=sys.stderr)
            continue
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        content = replace_msgstr(content, CONFIG_MSGID,      pairs["config"])
        content = replace_msgstr(content, DB_MSGID,          pairs["db"])
        content = replace_msgstr(content, BADGE_LIGHT_ONLY,  pairs["light_only"])
        content = replace_msgstr(content, BADGE_DARK_ONLY,   pairs["dark_only"])
        content = replace_msgstr(content, BADGE_DARK_LIGHT,  pairs["dark_light"])
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"updated: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "po"))
