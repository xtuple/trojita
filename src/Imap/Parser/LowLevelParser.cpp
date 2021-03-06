/* Copyright (C) 2006 - 2013 Jan Kundrát <jkt@flaska.net>

   This file is part of the Trojita Qt IMAP e-mail client,
   http://trojita.flaska.net/

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License or (at your option) version 3 or any later version
   accepted by the membership of KDE e.V. (or its successor approved
   by the membership of KDE e.V.), which shall act as a proxy
   defined in Section 14 of version 3 of the license.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QPair>
#include <QStringList>
#include <QVariant>
#include <QDateTime>
#include "LowLevelParser.h"
#include "../Exceptions.h"
#include "Imap/Encoders.h"

namespace Imap
{
namespace LowLevelParser
{

uint getUInt(const QByteArray &line, int &start)
{
    if (start == line.size())
        throw NoData("getUInt: no data", line, start);

    QByteArray item;
    bool breakIt = false;
    while (!breakIt && start < line.size()) {
        switch (line[start]) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            item.append(line[start]);
            ++start;
            break;
        default:
            breakIt = true;
            break;
        }
    }

    bool ok;
    uint number = item.toUInt(&ok);
    if (!ok)
        throw ParseError("getUInt: not a number", line, start);
    return number;
}

QByteArray getAtom(const QByteArray &line, int &start)
{
    if (start == line.size())
        throw NoData("getAtom: no data", line, start);

    int old(start);
    bool breakIt = false;
    while (!breakIt && start < line.size()) {
        if (line[start] <= '\x1f') {
            // CTL characters (excluding 0x7f) as defined in ABNF
            breakIt = true;
            break;
        }
        switch (line[start]) {
        case '(': case ')': case '{': case '\x20': case '\x7f':
        case '%': case '*': case '"': case '\\': case ']':
            breakIt  = true;
            break;
        default:
            ++start;
        }
    }

    if (old == start)
        throw ParseError("getAtom: did not read anything", line, start);
    return line.mid(old, start - old);
}

QPair<QByteArray,ParsedAs> getString(const QByteArray &line, int &start)
{
    if (start == line.size())
        throw NoData("getString: no data", line, start);

    if (line[start] == '"') {
        // quoted string
        ++start;
        bool escaping = false;
        QByteArray res;
        bool terminated = false;
        while (start != line.size() && !terminated) {
            if (escaping) {
                escaping = false;
                if (line[start] == '"' || line[start] == '\\')
                    res.append(line[start]);
                else
                    throw UnexpectedHere("getString: escaping invalid character", line, start);
            } else {
                switch (line[start]) {
                case '"':
                    terminated = true;
                    break;
                case '\\':
                    escaping = true;
                    break;
                case '\r': case '\n':
                    throw ParseError("getString: premature end of quoted string", line, start);
                default:
                    res.append(line[start]);
                }
            }
            ++start;
        }
        if (!terminated)
            throw NoData("getString: unterminated quoted string", line, start);
        return qMakePair(res, QUOTED);
    } else if (line[start] == '{') {
        // literal
        ++start;
        int size = getUInt(line, start);
        if (line.mid(start, 3) != "}\r\n")
            throw ParseError("getString: malformed literal specification", line, start);
        start += 3;
        if (start + size > line.size())
            throw NoData("getString: run out of data", line, start);
        int old(start);
        start += size;
        return qMakePair(line.mid(old, size), LITERAL);
    } else if (start < line.size() - 3 && line[start] == '~' && line[start + 1] == '{' ) {
        // literal8
        start += 2;
        int size = getUInt(line, start);
        if (line.mid(start, 3) != "}\r\n")
            throw ParseError("getString: malformed literal8 specification", line, start);
        start += 3;
        if (start + size > line.size())
            throw NoData("getString: literal8: run out of data", line, start);
        int old(start);
        start += size;
        return qMakePair(line.mid(old, size), LITERAL8);
    } else {
        throw UnexpectedHere("getString: did not get quoted string or literal", line, start);
    }
}

QPair<QByteArray,ParsedAs> getAString(const QByteArray &line, int &start)
{
    if (start >= line.size())
        throw NoData("getAString: no data", line, start);

    if (line[start] == '{' || line[start] == '"' || line[start] == '~')
        return getString(line, start);
    else
        return qMakePair(getAtom(line, start), ATOM);
}

QPair<QByteArray,ParsedAs> getNString(const QByteArray &line, int &start)
{
    QPair<QByteArray,ParsedAs> r = getAString(line, start);
    if (r.second == ATOM && r.first.toUpper() == "NIL") {
        r.first.clear();
        r.second = NIL;
    }
    return r;
}

QString getMailbox(const QByteArray &line, int &start)
{
    QPair<QByteArray,ParsedAs> r = getAString(line, start);
    if (r.first.toUpper() == "INBOX")
        return QLatin1String("INBOX");
    else
        return decodeImapFolderName(r.first);

}

QVariantList parseList(const char open, const char close, const QByteArray &line, int &start)
{
    if (start >= line.size())
        throw NoData("Could not parse list: no more data", line, start);

    if (line[start] == open) {
        // found the opening parenthesis
        ++start;
        if (start >= line.size())
            throw NoData("Could not parse list: just the opening bracket", line, start);

        QVariantList res;
        if (line[start] == close) {
            ++start;
            return res;
        }
        while (line[start] != close) {
            // We want to be benevolent here and eat extra whitespace
            eatSpaces(line, start);
            res.append(getAnything(line, start));
            if (start >= line.size())
                throw NoData("Could not parse list: truncated data", line, start);
            // Eat whitespace after each token, too
            eatSpaces(line, start);
            if (line[start] == close) {
                ++start;
                return res;
            }
        }
        return res;
    } else {
        throw UnexpectedHere(std::string("Could not parse list: expected a list enclosed in ")
                             + open + close + ", but got something else instead", line, start);
    }
}

QVariant getAnything(const QByteArray &line, int &start)
{
    if (start >= line.size())
        throw NoData("getAnything: no data", line, start);

    if (line[start] == '[') {
        QVariant res = parseList('[', ']', line, start);
        return res;
    } else if (line[start] == '(') {
        QVariant res = parseList('(', ')', line, start);
        return res;
    } else if (line[start] == '"' || line[start] == '{' || line[start] == '~') {
        QPair<QByteArray,ParsedAs> res = getString(line, start);
        return res.first;
    } else if (line.mid(start, 3).toUpper() == "NIL") {
        start += 3;
        return QByteArray();
    } else if (line[start] == '\\') {
        // valid for "flag"
        ++start;
        if (start >= line.size())
            throw NoData("getAnything: backslash-nothing is invalid", line, start);
        if (line[start] == '*') {
            ++start;
            return QByteArray("\\*");
        }
        return QByteArray(1, '\\') + getAtom(line, start);
    } else {
        switch (line.at(start)) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return getUInt(line, start);
            break;
        default:
        {
            QByteArray atom = getAtom(line, start);
            if (atom.indexOf('[', 0) != -1) {
                // "BODY[something]" -- there's no whitespace between "[" and
                // next atom...
                int pos = line.indexOf(']', start);
                if (pos == -1)
                    throw ParseError("getAnything: can't find ']' for the '['", line, start);
                ++pos;
                atom += line.mid(start, pos - start);
                start = pos;
                if (start < line.size() && line[start] == '<') {
                    // Let's check if it continues with "<range>"
                    pos = line.indexOf('>', start);
                    if (pos == -1)
                        throw ParseError("getAnything: can't find proper <range>", line, start);
                    ++pos;
                    atom += line.mid(start, pos - start);
                    start = pos;
                }
            }
            return atom;
        }
        }
    }
}

QList<uint> getSequence(const QByteArray &line, int &start)
{
    uint num = LowLevelParser::getUInt(line, start);
    if (start >= line.size() - 2) {
        // It's definitely just a number because there's no more data in here
        return QList<uint>() << num;
    } else {
        QList<uint> numbers;
        numbers << num;

        enum {COMMA, RANGE} currentType = COMMA;

        // Try to find further items in the sequence set
        while (line[start] == ':' || line[start] == ',') {
            // it's a sequence set

            if (line[start] == ':') {
                if (currentType == RANGE) {
                    // Now "x:y:z" is a funny syntax
                    throw UnexpectedHere("Sequence set: range cannot me defined by three numbers", line, start);
                }
                currentType = RANGE;
            } else {
                currentType = COMMA;
            }

            ++start;
            if (start >= line.size() - 2) throw NoData("Truncated sequence set", line, start);

            uint num = LowLevelParser::getUInt(line, start);
            if (currentType == COMMA) {
                // just adding one more to the set
                numbers << num;
            } else {
                // working with a range
                if (numbers.last() >= num)
                    throw UnexpectedHere("Sequence set contains an invalid range. "
                                         "First item of a range must always be smaller than the second item.", line, start);

                for (uint i = numbers.last() + 1; i <= num; ++i)
                    numbers << i;
            }
        }
        return numbers;
    }
}

QDateTime parseRFC2822DateTime(const QString &string)
{
    QStringList monthNames = QStringList() << QLatin1String("jan") << QLatin1String("feb") << QLatin1String("mar")
                                           << QLatin1String("apr") << QLatin1String("may") << QLatin1String("jun")
                                           << QLatin1String("jul") << QLatin1String("aug") << QLatin1String("sep")
                                           << QLatin1String("oct") << QLatin1String("nov") << QLatin1String("dec");

    QRegExp rx(QString::fromUtf8("^(?:\\s*([A-Z][a-z]+)\\s*,\\s*)?"   // date-of-week
                                 "(\\d{1,2})\\s+(%1)\\s+(\\d{2,4})" // date
                                 "\\s+(\\d{1,2})\\s*:(\\d{1,2})\\s*(?::\\s*(\\d{1,2})\\s*)?" // time
                                 "(\\s+(?:(?:([+-]?)(\\d{2})(\\d{2}))|(UT|GMT|EST|EDT|CST|CDT|MST|MDT|PST|PDT|[A-IK-Za-ik-z])))?" // timezone
                                 ).arg(monthNames.join(QLatin1String("|"))), Qt::CaseInsensitive);
    int pos = rx.indexIn(string);

    if (pos == -1)
        throw ParseError("Date format not recognized");

    QStringList list = rx.capturedTexts();

    if (list.size() != 13)
        throw ParseError("Date regular expression returned weird data (internal error?)");

    int year = list[4].toInt();
    int month = monthNames.indexOf(list[3].toLower()) + 1;
    if (month == 0)
        throw ParseError("Invalid month name");
    int day = list[2].toInt();
    int hours = list[5].toInt();
    int minutes = list[6].toInt();
    int seconds = list[7].toInt();
    int shift = list[10].toInt() * 60 + list[11].toInt();
    if (list[9] == QLatin1String("-"))
        shift *= 60;
    else
        shift *= -60;
    if (! list[12].isEmpty()) {
        const QString tz = list[12].toUpper();
        if (tz == QLatin1String("UT") || tz == QLatin1String("GMT"))
            shift = 0;
        else if (tz == QLatin1String("EST"))
            shift = 5 * 3600;
        else if (tz == QLatin1String("EDT"))
            shift = 4 * 3600;
        else if (tz == QLatin1String("CST"))
            shift = 6 * 3600;
        else if (tz == QLatin1String("CDT"))
            shift = 5 * 3600;
        else if (tz == QLatin1String("MST"))
            shift = 7 * 3600;
        else if (tz == QLatin1String("MDT"))
            shift = 6 * 3600;
        else if (tz == QLatin1String("PST"))
            shift = 8 * 3600;
        else if (tz == QLatin1String("PDT"))
            shift = 7 * 3600;
        else if (tz.size() == 1)
            shift = 0;
        else
            throw ParseError("Invalid TZ specification");
    }

    QDateTime date(QDate(year, month, day), QTime(hours, minutes, seconds), Qt::UTC);
    date = date.addSecs(shift);

    return date;
}

void eatSpaces(const QByteArray &line, int &start)
{
    while (line.size() > start && line[start] == ' ')
        ++start;
}

}
}
