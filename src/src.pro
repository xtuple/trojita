TEMPLATE = subdirs
SUBDIRS  = Imap MSA Streams qwwsmtpclient Common Composer
CONFIG += ordered

trojita_harmattan {
    SUBDIRS += Harmattan
} else {
    SUBDIRS += Gui
    XtConnect:SUBDIRS += XtConnect
}

CODECFORTR = UTF-8
