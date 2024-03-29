import os
import re
import subprocess

"""Fetch the .po files from KDE's SVN for Trojita

Run me from Trojita's top-level directory.
"""


SVN_PATH = "svn://anonsvn.kde.org/home/kde/trunk/l10n-kf5/"
SOURCE_PO_PATH = "/messages/trojita/trojita_common.po"
OUTPUT_PO_PATH = "./po/"
OUTPUT_PO_PATTERN = "trojita_common_%s.po"

fixer = re.compile(r'^#~\| ', re.MULTILINE)
re_empty_msgid = re.compile('^msgid ""$', re.MULTILINE)
re_empty_line = re.compile('^$', re.MULTILINE)
re_has_qt_contexts = re.compile('X-Qt-Contexts: true\\n')

if not os.path.exists(OUTPUT_PO_PATH):
    os.mkdir(OUTPUT_PO_PATH)

all_languages = subprocess.check_output(['svn', 'cat', SVN_PATH + 'subdirs'],
                                       stderr=subprocess.STDOUT)

all_languages = [x.strip() for x in all_languages.split("\n") if len(x)]
for lang in all_languages:
    try:
        raw_data = subprocess.check_output(['svn', 'cat', SVN_PATH + lang + SOURCE_PO_PATH],
                                          stderr=subprocess.PIPE)
        (transformed, subs) = fixer.subn('# ~| ', raw_data)
        pos1 = re_empty_msgid.search(transformed).start()
        pos2 = re_empty_line.search(transformed).start()
        if re_has_qt_contexts.search(transformed, pos1, pos2) is None:
            transformed = transformed[:pos2] + \
                    '"X-Qt-Contexts: true\\n"\n' + \
                    transformed[pos2:]
            subs = subs + 1
        if (subs > 0):
            print "Fetched %s (and performed %d cleanups)" % (lang, subs)
        else:
            print "Fetched %s" % lang
        file(OUTPUT_PO_PATH + OUTPUT_PO_PATTERN % lang, "wb").write(transformed)
    except subprocess.CalledProcessError:
        print "No data for %s" % lang

# Inform qmake about the updated file list
os.utime("CMakeLists.txt", None)
