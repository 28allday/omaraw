#include "xmppreset.h"
#include "toneregions.h"
#include "cameraprofiles.h"
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <cmath>
#include <utility>
#include <vector>
#include <algorithm>
#include <QHash>

namespace {
const QString crs = QStringLiteral("http://ns.adobe.com/camera-raw-settings/1.0/");
const QString rdf = QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#");
constexpr int maxDepth = 32, maxItems = 20000;

// The source editor's old format is a literal Lua table, not a program. Accept only
// data, including quoted keys, numeric arrays, comments and escaped strings.
class TableReader {
  public:
    explicit TableReader(QByteArray data) : text(std::move(data)) {
        if (text.startsWith("\xef\xbb\xbf"))
            pos = 3;
    }
    QVariantMap read() {
        space();
        if (text.mid(pos, 6) == "return" && (pos + 6 == text.size() || !identifier(text[pos + 6])))
            pos += 6;
        else {
            if (word() != "s" || !take('='))
                fail("Expected a XMP preset table.");
        }
        QVariant value = table(0);
        space();
        take(';');
        space();
        if (pos != text.size())
            fail("Executable or trailing content is not a preset.");
        return value.toMap();
    }
    QString error;

  private:
    QByteArray text;
    qsizetype pos = 0;
    int items = 0;
    static bool identifier(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }
    void fail(const char *message) {
        if (error.isEmpty())
            error = QString::fromLatin1(message);
    }
    bool longString(QByteArray &out) {
        if (pos >= text.size() || text[pos] != '[')
            return false;
        qsizetype next = pos + 1;
        while (next < text.size() && text[next] == '=')
            ++next;
        if (next >= text.size() || text[next] != '[')
            return false;
        const QByteArray closing = "]" + QByteArray(next - pos - 1, '=') + "]";
        const qsizetype end = text.indexOf(closing, next + 1);
        if (end < 0) {
            fail("Unterminated long string or comment.");
            pos = text.size();
            return true;
        }
        out = text.mid(next + 1, end - next - 1);
        if (out.startsWith("\r\n"))
            out.remove(0, 2);
        else if (out.startsWith('\n'))
            out.remove(0, 1);
        pos = end + closing.size();
        return true;
    }
    void space() {
        while (pos < text.size() && error.isEmpty()) {
            if (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n') {
                ++pos;
                continue;
            }
            if (text.mid(pos, 2) != "--")
                break;
            pos += 2;
            QByteArray ignored;
            if (!longString(ignored)) {
                while (pos < text.size() && text[pos] != '\n')
                    ++pos;
            }
        }
    }
    bool take(char c) {
        space();
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    QByteArray word() {
        space();
        const auto start = pos;
        while (pos < text.size() && identifier(text[pos]))
            ++pos;
        return text.mid(start, pos - start);
    }
    QString string() {
        space();
        QByteArray bytes;
        if (!longString(bytes)) {
            if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
                fail("Expected a quoted string.");
                return {};
            }
            const char quote = text[pos++];
            bool closed = false;
            while (pos < text.size() && error.isEmpty()) {
                char c = text[pos++];
                if (c == quote) {
                    closed = true;
                    break;
                }
                if (c == '\n' || c == '\r') {
                    fail("Unescaped newline in a string.");
                    break;
                }
                if (c != '\\') {
                    bytes += c;
                    continue;
                }
                if (pos == text.size())
                    break;
                c = text[pos++];
                if (c >= '0' && c <= '9') {
                    int n = c - '0';
                    for (int i = 0; i < 2 && pos < text.size() && text[pos] >= '0' && text[pos] <= '9'; ++i)
                        n = n * 10 + (text[pos++] - '0');
                    if (n > 255)
                        fail("Invalid string escape.");
                    else
                        bytes += char(n);
                } else if (c == 'n' || c == '\n')
                    bytes += '\n';
                else if (c == 'r')
                    bytes += '\r';
                else if (c == 't')
                    bytes += '\t';
                else if (c == 'a')
                    bytes += '\a';
                else if (c == 'b')
                    bytes += '\b';
                else if (c == 'f')
                    bytes += '\f';
                else if (c == 'v')
                    bytes += '\v';
                else if (c == '\\' || c == '"' || c == '\'')
                    bytes += c;
                else
                    fail("Unsupported string escape.");
            }
            if (!closed)
                fail("Unterminated string.");
        }
        QStringDecoder decode(QStringDecoder::Utf8);
        const QString result = decode(bytes);
        if (decode.hasError())
            fail("The preset must use UTF-8 text.");
        return result;
    }
    QVariant value(int depth) {
        space();
        if (++items > maxItems || depth > maxDepth) {
            fail("Preset nesting or item limit exceeded.");
            return {};
        }
        if (pos >= text.size()) {
            fail("Incomplete preset.");
            return {};
        }
        const char c = text[pos];
        if (c == '{')
            return table(depth);
        if (c == '\'' || c == '"' || c == '[')
            return string();
        if (text.mid(pos, 4) == "true" && (pos + 4 == text.size() || !identifier(text[pos + 4]))) {
            pos += 4;
            return true;
        }
        if (text.mid(pos, 5) == "false" && (pos + 5 == text.size() || !identifier(text[pos + 5]))) {
            pos += 5;
            return false;
        }
        const auto start = pos;
        while (pos < text.size() && QByteArray("0123456789.+-eE").contains(text[pos]))
            ++pos;
        bool ok = false;
        const double n = text.mid(start, pos - start).toDouble(&ok);
        if (!ok || !std::isfinite(n))
            fail("Only finite numbers, strings, booleans and tables are allowed.");
        return n;
    }
    QVariant table(int depth) {
        if (depth > maxDepth || !take('{')) {
            fail("Expected a literal table.");
            return {};
        }
        QVariantMap map;
        QVariantList list;
        while (error.isEmpty()) {
            if (take('}'))
                break;
            space();
            const auto start = pos;
            QString key;
            bool keyed = false;
            if (pos < text.size() && ((text[pos] >= 'A' && text[pos] <= 'Z') ||
                                      (text[pos] >= 'a' && text[pos] <= 'z') || text[pos] == '_')) {
                key = QString::fromLatin1(word());
                keyed = take('=');
                if (!keyed)
                    pos = start;
            } else if (pos < text.size() && text[pos] == '[') {
                QByteArray ignored;
                if (!longString(ignored)) {
                    ++pos;
                    key = string();
                    if (!take(']') || !take('='))
                        fail("Invalid table key.");
                    keyed = true;
                }
                pos = keyed ? pos : start;
            }
            const QVariant item = value(depth + 1);
            if (keyed) {
                if (map.contains(key) || !list.isEmpty())
                    fail("Duplicate keys or mixed tables are not supported.");
                map.insert(key, item);
            } else {
                if (!map.isEmpty())
                    fail("Mixed tables are not supported.");
                list << item;
            }
            space();
            if (pos < text.size() && text[pos] == '}')
                continue;
            if (!take(',') && !take(';'))
                fail("Expected a table separator.");
        }
        return map.isEmpty() ? QVariant(list) : QVariant(map);
    }
};

struct Element {
    QString ns, name, text, language;
    QMap<QString, QString> attributes;
    QList<Element> children;
};
Element element(QXmlStreamReader &xml, int depth, int &count) {
    Element out;
    if (depth > maxDepth || ++count > maxItems) {
        xml.raiseError("Preset nesting or item limit exceeded.");
        return out;
    }
    out.ns = xml.namespaceUri().toString();
    out.name = xml.name().toString();
    for (const auto &a : xml.attributes()) {
        if (a.namespaceUri() == crs)
            out.attributes.insert(a.name().toString(), a.value().toString());
        if (a.namespaceUri() == QLatin1String("http://www.w3.org/XML/1998/namespace") &&
            a.name() == QLatin1String("lang"))
            out.language = a.value().toString();
    }
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement())
            out.children << element(xml, depth + 1, count);
        else if (xml.isCharacters())
            out.text += xml.text();
        else if (xml.isEndElement())
            break;
        else if (xml.isDTD() || xml.isEntityReference())
            xml.raiseError("DTD and entity references are not supported.");
    }
    return out;
}
QVariant content(const Element &node) {
    if (node.children.isEmpty())
        return node.text.trimmed();
    const Element &container = node.children.first();
    if (node.children.size() == 1 && container.ns == rdf) {
        if (container.name == QLatin1String("Alt")) {
            QString first;
            for (const auto &li : container.children)
                if (li.ns == rdf && li.name == QLatin1String("li")) {
                    if (first.isEmpty())
                        first = li.text.trimmed();
                    if (li.language == QLatin1String("x-default"))
                        return li.text.trimmed();
                }
            return first;
        }
        if (container.name == QLatin1String("Seq") || container.name == QLatin1String("Bag")) {
            QVariantList values;
            for (const auto &li : container.children)
                if (li.ns == rdf && li.name == QLatin1String("li"))
                    values << content(li);
            return values;
        }
    }
    return QVariantMap{{QStringLiteral("structured"), true}};
}
void collect(const Element &node, QVariantMap &fields, QString &error) {
    // Settings are properties of the outer RDF description. Never treat
    // local-mask descendants as global adjustments.
    if (node.ns == rdf && node.name == QLatin1String("Description")) {
        auto add = [&](const QString &name, const QVariant &value) {
            if (fields.contains(name))
                error = "Duplicate CRS property: " + name;
            else
                fields.insert(name, value);
        };
        for (auto it = node.attributes.cbegin(); it != node.attributes.cend(); ++it)
            add(it.key(), it.value());
        for (const auto &child : node.children)
            if (child.ns == crs)
                add(child.name, content(child));
        return;
    }
    for (const auto &child : node.children)
        collect(child, fields, error);
}

QVariantMap xmp(const QByteArray &bytes, QString &error) {
    QXmlStreamReader xml(bytes);
    QVariantMap fields;
    int count = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto root = element(xml, 0, count);
            collect(root, fields, error);
        } else if (xml.isDTD() || xml.isEntityReference())
            xml.raiseError("DTD and entity references are not supported.");
    }
    if (xml.hasError())
        error = xml.errorString();
    return fields;
}

bool flag(const QVariant &value, bool &ok) {
    if (value.metaType().id() == QMetaType::Bool) {
        ok = true;
        return value.toBool();
    }
    const QString s = value.toString().trimmed();
    ok = s.compare("true", Qt::CaseInsensitive) == 0 || s.compare("false", Qt::CaseInsensitive) == 0;
    return s.compare("true", Qt::CaseInsensitive) == 0;
}
void convertDevelop(const QVariantMap &fields, XmpPreset::Result &out, bool sidecar, bool raw);
} // namespace

QVariantMap XmpPreset::readXmpProperties(const QByteArray &bytes, QString &error) {
    if (bytes.size() > 32 * 1024 * 1024) { error = "XMP is larger than 32 MiB."; return {}; }
    return xmp(bytes, error);
}

XmpPreset::Result XmpPreset::read(const QByteArray &bytes, const QString &fileName) {
    Result out;
    if (bytes.size() > 4 * 1024 * 1024) {
        out.error = "XMP presets must be smaller than 4 MiB.";
        return out;
    }
    QVariantMap fields;
    if (QFileInfo(fileName).suffix().compare("lrtemplate", Qt::CaseInsensitive) == 0) {
        TableReader parser(bytes);
        const auto root = parser.read();
        out.error = parser.error;
        if (!out.error.isEmpty())
            return out;
        if (root.value("type").toString() != QLatin1String("Develop")) {
            out.error = "This is not an XMP Develop preset.";
            return out;
        }
        out.name = root.value("title", root.value("internalName")).toString().trimmed();
        fields = root.value("value").toMap().value("settings").toMap();
        out.category = "Imported XMP";
    } else {
        fields = xmp(bytes, out.error);
        if (!out.error.isEmpty())
            return out;
        const QString type = fields.value("PresetType").toString();
        if ((!type.isEmpty() && type != QLatin1String("Normal")) || (type.isEmpty() && !fields.contains("Name"))) {
            out.error = "This XMP is not an XMP Develop preset. Import enhanced profiles from "
                        "Develop → Look → Creative profile → Import profile.";
            return out;
        }
        out.name = fields.value("Name").toString().trimmed();
        out.category = fields.value("Group").toString().simplified();
        if (out.category.isEmpty())
            out.category = "Imported XMP";
    }
    if (out.name.isEmpty())
        out.name = QFileInfo(fileName).completeBaseName().trimmed();
    if (out.name.isEmpty()) {
        out.error = "The preset needs a name or a filename.";
        return out;
    }
    if (out.name.size() > 512 || out.category.size() > 512) {
        out.error = "The preset name or group is too long.";
        return out;
    }
    if (fields.isEmpty()) {
        out.error = "The preset has no Develop settings.";
        return out;
    }

    convertDevelop(fields, out, false, false);
    return out;
}

XmpPreset::Result XmpPreset::readSidecar(const QByteArray &bytes, bool raw) {
    Result out;
    out.name = QStringLiteral("XMP edit");
    if (bytes.size() > 8 * 1024 * 1024) { out.error = "The sidecar is larger than 8 MiB."; return out; }
    const QVariantMap fields = xmp(bytes, out.error);
    if (!out.error.isEmpty()) return out;
    if (fields.isEmpty()) { out.error = "The sidecar has no CRS settings."; return out; }
    convertDevelop(fields, out, true, raw);
    return out;
}

namespace {
// The conversion itself, for a preset or for a photo's own sidecar. In a
// sidecar exports include every slider, most at their defaults: those are
// left to OmaRAW's own defaults rather than forced, and exposure is taken
// from where OmaRAW's rendering of a RAW starts (+0.7 EV), as the source editor's 0
// is its own normal rendering.
void convertDevelop(const QVariantMap &fields, XmpPreset::Result &out, bool sidecar, bool raw) {
    QSet<QString> used;
    auto warn = [&](const QString &key, const QString &why) {
        used.insert(key);
        out.warnings << key + ": " + why;
    };
    auto number = [&](const QString &key, double low, double high, double &n) {
        used.insert(key);
        bool ok = false;
        const auto value = fields.value(key);
        n = value.toString().toDouble(&ok);
        if (value.metaType().id() == QMetaType::Bool || !ok || !std::isfinite(n) || n < low || n > high) {
            out.error = "Invalid or out-of-range setting: " + key;
            return false;
        }
        return true;
    };
    auto put = [&](const char *op, const char *field, double value) {
        out.values << QVariantMap{{"op", QString::fromLatin1(op)},
                                  {"field", QString::fromLatin1(field)},
                                  {"value", value},
                                  {"enabled", true}};
    };
    auto enabled = [&](const QString &setting, const QString &group) {
        if (!fields.contains(group))
            return true;
        bool ok = false;
        const bool on = flag(fields.value(group), ok);
        used.insert(group);
        if (!ok)
            out.error = "Invalid switch: " + group;
        if (!on)
            warn(setting, "not converted because its panel is disabled in the source preset");
        return ok && on;
    };
    auto automatic = [&](const QString &name) {
        if (!fields.contains(name))
            return false;
        bool ok = false;
        const bool on = flag(fields.value(name), ok);
        used.insert(name);
        if (!ok)
            out.error = "Invalid switch: " + name;
        if (on)
            out.warnings << name + ": automatic adjustments need the original photo and were not converted";
        return on;
    };
    // In a sidecar, a setting at the source editor's own default stays at OmaRAW's.
    auto atDefault = [&](const QString &key, double n, double def) {
        if (!sidecar || n != def) return false;
        used.insert(key);
        return true;
    };
    const bool autoTone = automatic("AutoTone"), autoExposure = automatic("AutoExposure"),
               autoContrast = automatic("AutoContrast");
    const QString exposure = fields.contains("Exposure2012") ? "Exposure2012" : "Exposure";
    if (fields.contains(exposure)) {
        double n;
        if (number(exposure, -5, 5, n) && enabled(exposure, "EnableBasicAdjustments")) {
            if (autoTone || autoExposure)
                warn(exposure, "automatic exposure is not converted");
            else if (!atDefault(exposure, n, 0)) {
                put("exposure", "exposure", n + (sidecar && raw ? 0.7 : 0));
                // A preset does not know the photo it will land on: its
                // exposure is the source editor's, relative to the photo's starting
                // exposure (+0.7 on a RAW, 0 on anything else), added when
                // it is applied (EngineWorker::setParams).
                if (!sidecar) {
                    QVariantMap row = out.values.last().toMap();
                    row[QStringLiteral("fromStart")] = true;
                    out.values.last() = row;
                }
                out.converted << exposure + " → Exposure (EV)";
            }
        }
    }
    const QString contrast = fields.contains("Contrast2012") ? "Contrast2012" : "Contrast";
    if (fields.contains(contrast)) {
        double n;
        if (number(contrast, contrast == "Contrast" ? -50 : -100, 100, n) &&
            enabled(contrast, "EnableBasicAdjustments")) {
            if (autoTone || autoContrast)
                warn(contrast, "automatic contrast is not converted");
            else if (!atDefault(contrast, n, 0)) {
                put("sigmoid", "middle_grey_contrast",
                    1.5 * std::pow(2.0, (n - (contrast == "Contrast" ? 25 : 0)) / 100.0));
                out.converted << contrast + " → Tone contrast (approximate)";
            }
        }
    }
    for (const auto &pair : {qMakePair("Saturation", "saturation_global"), qMakePair("Vibrance", "vibrance")})
        if (fields.contains(pair.first)) {
            double n;
            if (number(pair.first, -100, 100, n) && enabled(pair.first, "EnableColorAdjustments") && !atDefault(pair.first, n, 0)) {
                put("colorbalancergb", pair.second, n / 100);
                if (QString::fromLatin1(pair.first) == "Saturation")
                    put("colorbalancergb", "chroma_global", n == -100 ? -1 : 0);
                out.converted << QString::fromLatin1(pair.first) + " → Colour (approximate)";
            }
        }
    bool mono = false;
    if (fields.contains("ConvertToGrayscale")) {
        bool ok = false;
        const bool on = flag(fields.value("ConvertToGrayscale"), ok);
        mono = ok && on;
        used.insert("ConvertToGrayscale");
        if (!ok)
            out.error = "Invalid switch: ConvertToGrayscale";
        else if (on) {
            out.values << QVariantMap{{"op", "monochrome"}, {"enabled", true}};
            // Preserve incoming colour for the converter's hue bands.
            put("colorbalancergb", "saturation_global", 0);
            put("colorbalancergb", "chroma_global", 0);
            put("colorbalancergb", "vibrance", 0);
            // A previous primary tint runs after the converter. New split
            // toning in this preset is applied below, after these resets.
            for (const char *field : {"lift_C", "gamma_C", "gain_C", "offset_C"})
                put("omarawgrade", field, 0);
            out.converted << "Black-and-white → monochrome";
        } else {
            out.values << QVariantMap{{"op", "monochrome"}, {"enabled", false}};
            out.converted << "Colour mode → Black and white disabled";
        }
    }
    const char *lrBands[] = {"Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"};
    const char *omaBands[] = {"red", "orange", "yellow", "green", "cyan", "blue", "lavender", "magenta"};
    const char *monoBands[] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
    for (int i = 0; i < 8; ++i)
        for (const auto &pair : {qMakePair("HueAdjustment", "hue_"), qMakePair("SaturationAdjustment", "sat_"),
                                 qMakePair("LuminanceAdjustment", "bright_")}) {
            const QString key = QString::fromLatin1(pair.first) + QLatin1String(lrBands[i]);
            if (!fields.contains(key))
                continue;
            // In black and white the source editor keeps the colour mix but does not
            // use it; the grey mix below takes its place.
            if (mono) { used.insert(key); continue; }
            double n;
            if (number(key, -100, 100, n) && enabled(key, "EnableColorAdjustments") && !atDefault(key, n, 0)) {
                const QByteArray target = QByteArray(pair.second) + omaBands[i];
                put("colorequal", target.constData(),
                    QString::fromLatin1(pair.first) == "HueAdjustment" ? n * .3 : 1 + n / 100);
                out.converted << key + " → Colour mixer (approximate)";
            }
        }
    // The source editor's grey mix: how light each colour turns in black and white.
    // Use the dedicated converter. Explicit zero must reset an existing mix.
    if (mono)
        for (int i = 0; i < 8; ++i) {
            const QString key = QLatin1String("GrayMixer") + QLatin1String(lrBands[i]);
            if (!fields.contains(key)) continue;
            double n;
            if (number(key, -100, 100, n) && enabled(key, "EnableGrayscaleMix")) {
                put("monochrome", (QByteArray("mix_") + monoBands[i]).constData(), n / 100);
                out.converted << key + " → Black and white mix (approximate)";
            }
        }
    else
        for (const char *band : lrBands)
            if (fields.contains(QLatin1String("GrayMixer") + QLatin1String(band))) used.insert(QLatin1String("GrayMixer") + QLatin1String(band));
    // Calibration: each primary's hue turned and its saturation scaled.
    {
        const char *lr[] = {"Red", "Green", "Blue"}, *oma[] = {"red", "green", "blue"};
        for (int i = 0; i < 3; ++i) {
            double hue = 0, sat = 0;
            const QString hk = QLatin1String(lr[i]) + QLatin1String("Hue"), sk = QLatin1String(lr[i]) + QLatin1String("Saturation");
            const bool hasHue = fields.contains(hk), hasSat = fields.contains(sk);
            if (!hasHue && !hasSat) continue;
            if ((hasHue && !number(hk, -100, 100, hue)) || (hasSat && !number(sk, -100, 100, sat))) continue;
            if ((hue == 0 && sat == 0) || !enabled(hasHue ? hk : sk, "EnableCalibration")) continue;
            if (hue != 0) put("primaries", (QByteArray(oma[i]) + "_hue").constData(), hue / 100 * M_PI / 6);
            if (sat != 0) put("primaries", (QByteArray(oma[i]) + "_purity").constData(), 1 + sat / 200);
            out.converted << QLatin1String(lr[i]) + QString::fromUtf8(" primary → Primaries (approximate)");
        }
        if (fields.contains("ShadowTint")) {
            double n;
            if (number("ShadowTint", -100, 100, n) && n != 0) warn("ShadowTint", "not translated");
        }
    }
    // Approximate split toning with Primary correction Lift/Gain. Their
    // responses differ from tonal masks, but every imported tint remains
    // visible and editable. Full saturation uses each wheel's working range.
    {
        const char *zones[] = {"Shadow", "Highlight"}, *wheels[] = {"lift", "gain"};
        bool any = false;
        for (int i = 0; i < 2; ++i) {
            const QString hk = QLatin1String("SplitToning") + QLatin1String(zones[i]) + QLatin1String("Hue"),
                          sk = QLatin1String("SplitToning") + QLatin1String(zones[i]) + QLatin1String("Saturation");
            double hue = 0, sat = 0;
            if (fields.contains(hk) && !number(hk, 0, 360, hue)) continue;
            if (fields.contains(sk) && !number(sk, 0, 100, sat)) continue;
            if (sat == 0 || !enabled(sk, "EnableSplitToning")) continue;
            put("omarawgrade", (QByteArray(wheels[i]) + "_H").constData(), std::fmod(hue, 360.0));
            put("omarawgrade", (QByteArray(wheels[i]) + "_C").constData(), sat / 100 * (i == 0 ? 0.12 : 0.25));
            any = true;
        }
        if (any) out.converted << "Split toning → Primary correction, Lift and Gain (approximate)";
        if (fields.contains("SplitToningBalance")) {
            double n;
            if (number("SplitToningBalance", -100, 100, n) && n != 0 && any) warn("SplitToningBalance", "not translated");
        }
    }
    // Grain size: the source editor's 0..100 (25 is its default) onto the engine's
    // coarseness, its default at the same place.
    if (fields.contains("GrainSize")) {
        double n;
        if (number("GrainSize", 0, 100, n) && enabled("GrainSize", "EnableEffects") && !atDefault("GrainSize", n, 25)) {
            put("grain", "scale", (400 + 48 * n) / 213.2);
            out.converted << "GrainSize → Grain coarseness (approximate)";
        }
    }
    if (fields.contains("GrainFrequency")) {
        double n;
        if (number("GrainFrequency", 0, 100, n) && n != 50) warn("GrainFrequency", "roughness is not translated");
    }
    // The parametric curve: OmaRAW's Tone regions have the same four regions
    // at the source editor's default splits.
    {
        const char *keys[] = {"ParametricHighlights", "ParametricLights", "ParametricDarks", "ParametricShadows"};
        double sliders[4] = {0, 0, 0, 0};
        bool any = false, ok = true;
        for (int r = 0; r < 4; ++r)
            if (fields.contains(keys[r])) {
                ok &= number(keys[r], -100, 100, sliders[r]);
                any |= sliders[r] != 0;
            }
        const char *splits[] = {"ParametricShadowSplit", "ParametricMidtoneSplit", "ParametricHighlightSplit"};
        const double lrSplit[] = {25, 50, 75};
        bool moved = false;
        for (int i = 0; i < 3; ++i)
            if (fields.contains(splits[i])) {
                double n;
                ok &= number(splits[i], 0, 100, n);
                moved |= std::abs(n - lrSplit[i]) > 0.5;
            }
        if (ok && any && enabled(keys[0], "EnableToneCurve")) {
            QVariantList xs, ys;
            ToneRegions::curve(sliders, xs, ys);
            out.values << QVariantMap{{"tonecurveset", true}, {"enabled", true}, {"xs", xs}, {"ys", ys}};
            out.converted << "Parametric curve → Tone regions (approximate)";
            if (moved) out.warnings << "ParametricSplit: the region splits are not translated; Tone regions use the source editor's defaults";
        }
    }
    struct Mapping {
        const char *source, *op, *field, *group;
        double low, high, scale;
    };
    for (const Mapping &m : {Mapping{"Sharpness", "sharpen", "amount", "EnableDetail", 0, 150, .02},
                             {"SharpenRadius", "sharpen", "radius", "EnableDetail", .5, 3, 1},
                             {"GrainAmount", "grain", "strength", "EnableEffects", 0, 100, 1},
                             {"PostCropVignetteAmount", "vignette", "brightness", "EnableEffects", -100, 100, .01}}) {
        if (!fields.contains(m.source))
            continue;
        double n;
        const double lrDefault = QString::fromLatin1(m.source) == "Sharpness" ? (raw ? 40 : 0) : QString::fromLatin1(m.source) == "SharpenRadius" ? 1 : 0;
        if (atDefault(m.source, n = fields.value(m.source).toString().toDouble(), lrDefault)) continue;
        if (number(m.source, m.low, m.high, n) && enabled(m.source, m.group) &&
            (QString::fromLatin1(m.source) != "PostCropVignetteAmount" || enabled(m.source, "EnableVignettes"))) {
            double mapped = n * m.scale;
            if (QString::fromLatin1(m.source) == "Sharpness" && mapped > 2) {
                mapped = 2;
                out.warnings << "Sharpness: capped at OmaRAW's maximum of 2";
            }
            put(m.op, m.field, mapped);
            if (QString::fromLatin1(m.source) == "PostCropVignetteAmount")
                put("vignette", "saturation", 0);
            out.converted << QString::fromLatin1(m.source) + " (approximate)";
        }
    }

    // White balance: a RAW's is absolute (kelvin, and a source tint ranging
    // to ±150); "As Shot" is the camera's, which OmaRAW starts from anyway.
    {
        const QString wb = fields.value("WhiteBalance").toString();
        if (fields.contains("WhiteBalance")) used.insert("WhiteBalance");
        if (wb == QLatin1String("As Shot") || (!fields.contains("Temperature") && !fields.contains("Tint"))) {
            used.insert("Temperature"); used.insert("Tint");
        } else if (wb == QLatin1String("Auto")) {
            warn("WhiteBalance", "automatic white balance needs the original photo and was not converted");
            used.insert("Temperature"); used.insert("Tint");
        } else if (fields.contains("Temperature")) {
            double t = 0, tint = 0;
            if (number("Temperature", 2000, 50000, t) && enabled("Temperature", "EnableBasicAdjustments")) {
                put("channelmixerrgb", "temperature", t);
                if (fields.contains("Tint") && number("Tint", -150, 150, tint)) put("channelmixerrgb", "tint", tint * 100 / 150);
                out.converted << "Temperature/Tint → White balance (approximate)";
            }
        }
    }
    // Highlights and Shadows go together into one module, whose own defaults
    // would otherwise act on the one not given.
    if (fields.contains("Highlights2012") || fields.contains("Shadows2012")) {
        double h = 0, sh = 0;
        const bool ok = (!fields.contains("Highlights2012") || number("Highlights2012", -100, 100, h))
                     && (!fields.contains("Shadows2012") || number("Shadows2012", -100, 100, sh));
        if (ok && (h != 0 || sh != 0) && enabled("Highlights2012", "EnableBasicAdjustments")) {
            put("shadhi", "highlights", h);
            put("shadhi", "shadows", sh);
            out.converted << "Highlights/Shadows → Shadows and highlights (approximate)";
        }
    }
    struct Simple { const char *source, *op, *field; double low, high, scale; const char *what; };
    for (const Simple &m : {Simple{"Blacks2012", "exposure", "black", -100, 100, -0.0002, "Blacks → Black level (approximate)"},
                            {"Clarity2012", "bilat", "detail", -100, 100, 0.01, "Clarity → Local contrast (approximate)"},
                            {"Texture", "diffuse", "texture", -100, 100, 1, "Texture → Texture (approximate)"},
                            {"Dehaze", "hazeremoval", "strength", -100, 100, 0.01, "Dehaze → Dehaze (approximate)"}}) {
        if (!fields.contains(m.source)) continue;
        double n;
        if (!number(m.source, m.low, m.high, n)) continue;
        if (n == 0) continue;                       // The source editor's "none" is OmaRAW's module left off
        if (!enabled(m.source, QString::fromLatin1(m.source) == "Dehaze" ? "EnableEffects" : "EnableBasicAdjustments")) continue;
        put(m.op, m.field, n * m.scale);
        out.converted << QString::fromUtf8(m.what);   // the arrow is UTF-8
    }
    if (fields.contains("Whites2012")) {
        double n;
        if (number("Whites2012", -100, 100, n) && n != 0) warn("Whites2012", "not translated");
    }
    // Luminance noise reduction: OmaRAW's Detail ▸ Noise reduction,
    // Everything, at the same strength and kept detail.
    if (fields.contains("LuminanceSmoothing")) {
        double n, detail = 50;
        if (number("LuminanceSmoothing", 0, 100, n) && n > 0 && enabled("LuminanceSmoothing", "EnableDetail")) {
            if (fields.contains("LuminanceNoiseReductionDetail")) number("LuminanceNoiseReductionDetail", 0, 100, detail);
            put("denoiseprofile", "mode", 3);
            put("denoiseprofile", "strength", n / 50);
            put("denoiseprofile", "overshooting", 1);
            put("denoiseprofile", "central_pixel_weight", 0.1 + detail * 0.029);
            out.converted << "Luminance noise reduction → Noise reduction (approximate)";
        }
    }
    for (const char *quiet : {"LuminanceNoiseReductionDetail", "LuminanceNoiseReductionContrast", "ColorNoiseReductionDetail", "ColorNoiseReductionSmoothness"})
        if (fields.contains(quiet)) used.insert(quiet);
    if (fields.contains("ColorNoiseReduction")) {
        double n;
        if (number("ColorNoiseReduction", 0, 100, n) && n != 0 && n != 25)
            warn("ColorNoiseReduction", "colour noise is left to OmaRAW's own first edit");
    }
    // A crop without a turn; a turned crop depends on geometry OmaRAW does
    // not share with the source editor and is left for the person to redo.
    if (fields.contains("HasCrop")) {
        bool ok = false;
        const bool has = flag(fields.value("HasCrop"), ok);
        used.insert("HasCrop");
        double angle = 0;
        if (fields.contains("CropAngle")) number("CropAngle", -45, 45, angle);
        if (ok && has && qAbs(angle) > 0.01) {
            for (const char *k : {"CropLeft", "CropTop", "CropRight", "CropBottom"}) used.insert(k);
            warn("CropAngle", "a straightened crop is not converted; crop the photo again");
        } else if (ok && has) {
            double l = 0, t = 0, r = 1, b = 1;
            if (number("CropLeft", 0, 1, l) && number("CropTop", 0, 1, t) && number("CropRight", 0, 1, r) && number("CropBottom", 0, 1, b)
                && r > l && b > t) {
                put("crop", "cx", l); put("crop", "cy", t); put("crop", "cw", r); put("crop", "ch", b);
                out.converted << "Crop → Crop (check the frame)";
            }
        } else for (const char *k : {"CropLeft", "CropTop", "CropRight", "CropBottom", "CropAngle"}) used.insert(k);
    }
    if (fields.contains("LensProfileEnable")) {
        used.insert("LensProfileEnable");
        if (fields.value("LensProfileEnable").toString().trimmed() == QLatin1String("1")) {
            out.values << QVariantMap{{"op", QStringLiteral("lens")}, {"enabled", true}};
            out.converted << "Lens profile → Lens correction (OmaRAW's own lens database)";
        }
    }

    // OmaRAW has three RGB curves. Preserve supplied point coordinates when
    // there is a master alone, or channel curves with an identity master.
    // A non-linear master plus channel curves needs a different composition.
    const QString curveBase = fields.contains("ToneCurvePV2012") ? "ToneCurvePV2012" : "ToneCurve";
    auto curve = [&](const QString &key) -> QVariantMap {
        QVariantList xs, ys;
        const auto values = fields.value(key).toList();
        QVariantList points;
        if (!values.isEmpty() && values.first().metaType().id() == QMetaType::QString) {
            for (const auto &item : values) {
                const auto pair = item.toString().split(',');
                if (pair.size() != 2) {
                    out.error = "Invalid tone curve: " + key;
                    return {};
                }
                points << pair[0] << pair[1];
            }
        } else
            points = values;
        if (points.size() < 4 || points.size() % 2 || points.size() > 40) {
            out.error = "Tone curves need 2 to 20 points: " + key;
            return {};
        }
        double previous = -1;
        for (int i = 0; i < points.size(); i += 2) {
            bool xok = false, yok = false;
            const double x = points[i].toString().toDouble(&xok), y = points[i + 1].toString().toDouble(&yok);
            if (!xok || !yok || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 255 || y < 0 || y > 255 ||
                x <= previous) {
                out.error = "Invalid tone curve: " + key;
                return {};
            }
            xs << x / 255;
            ys << y / 255;
            previous = x;
        }
        // The source editor holds a curve level beyond its end points.
        if (xs.first().toDouble() > 0) { xs.prepend(0.0); ys.prepend(ys.first()); }
        if (xs.last().toDouble() < 1) { xs << 1.0; ys << ys.last(); }
        return {{"xs", xs}, {"ys", ys}, {"type", 2}};
    };
    const QVariantMap identity{{"xs", QVariantList{0.0, 1.0}}, {"ys", QVariantList{0.0, 1.0}}, {"type", 2}};
    // The source editor's curves are smooth splines through their points.
    auto eval = [](const QVariantMap &c, double x) {
        const QVariantList xl = c.value("xs").toList(), yl = c.value("ys").toList();
        const int n = int(xl.size());
        std::vector<double> X(n), Y(n), m(n, 0.0);
        for (int i = 0; i < n; ++i) { X[i] = xl[i].toDouble(); Y[i] = yl[i].toDouble(); }
        if (n > 2) {   // natural cubic spline: second derivatives by the tridiagonal sweep
            std::vector<double> u(n, 0.0);
            for (int i = 1; i < n - 1; ++i) {
                const double sig = (X[i] - X[i - 1]) / (X[i + 1] - X[i - 1]), p = sig * m[i - 1] + 2;
                m[i] = (sig - 1) / p;
                u[i] = (6 * ((Y[i + 1] - Y[i]) / (X[i + 1] - X[i]) - (Y[i] - Y[i - 1]) / (X[i] - X[i - 1])) / (X[i + 1] - X[i - 1]) - sig * u[i - 1]) / p;
            }
            m[n - 1] = 0;
            for (int i = n - 2; i >= 0; --i) m[i] = m[i] * m[i + 1] + u[i];
        }
        x = qBound(X.front(), x, X.back());
        int k = 0;
        while (k < n - 2 && x > X[k + 1]) ++k;
        const double h = X[k + 1] - X[k], a = (X[k + 1] - x) / h, b = (x - X[k]) / h;
        return qBound(0.0, a * Y[k] + b * Y[k + 1] + ((a * a * a - a) * m[k] + (b * b * b - b) * m[k + 1]) * h * h / 6, 1.0);
    };
    // A master curve followed by a colour curve, as one curve per colour:
    // sampled at the master's points and where the master reaches each of
    // the colour curve's points, so both keep their shape.
    auto compose = [&](const QVariantMap &master, const QVariantMap &colour) -> QVariantMap {
        std::vector<double> at;
        for (const auto &x : master.value("xs").toList()) at.push_back(x.toDouble());
        for (const auto &target : colour.value("xs").toList()) {
            const double y = target.toDouble();
            if (y < eval(master, 0) || y > eval(master, 1)) continue;
            double lo = 0, hi = 1;
            for (int i = 0; i < 40; ++i) { const double mid = (lo + hi) / 2; (eval(master, mid) < y ? lo : hi) = mid; }
            at.push_back((lo + hi) / 2);
        }
        std::sort(at.begin(), at.end());
        std::vector<double> xs;
        for (double x : at) if (xs.empty() || x - xs.back() > 1e-3) xs.push_back(x);
        if (xs.size() > 20) { xs.clear(); for (int i = 0; i < 20; ++i) xs.push_back(i / 19.0); }
        QVariantList ox, oy;
        for (double x : xs) { ox << x; oy << eval(colour, eval(master, x)); }
        return {{"xs", ox}, {"ys", oy}, {"type", 2}};
    };
    const bool hasMaster = fields.contains(curveBase);
    const QString rgbBase = (hasMaster ? curveBase : QStringLiteral("ToneCurvePV2012"));
    QStringList curveKeys;
    if (hasMaster)
        curveKeys << curveBase;
    for (const auto *channel : {"Red", "Green", "Blue"})
        if (fields.contains(rgbBase + QLatin1String(channel)))
            curveKeys << rgbBase + QLatin1String(channel);
    if (!curveKeys.isEmpty()) {
        QVariantMap master = hasMaster ? curve(curveBase) : identity;
        QVariantList channels;
        bool coloured = false;
        for (const auto *channel : {"Red", "Green", "Blue"}) {
            const QString key = rgbBase + QLatin1String(channel);
            const auto c = fields.contains(key) ? curve(key) : identity;
            channels << c;
            coloured |= c.value("xs") != c.value("ys");
        }
        if (sidecar && master.value("xs") == master.value("ys") && !coloured) {
            // A straight line: the source editor writes one for every photo.
            for (const auto &key : curveKeys) used.insert(key);
            used.insert(curveBase == "ToneCurvePV2012" ? "ToneCurveName2012" : "ToneCurveName");
        } else if (enabled(curveKeys.first(), "EnableToneCurve")) {
            if (!coloured)
                channels = QVariantList{master, master, master};
            else if (master.value("xs") != master.value("ys"))
                for (auto &c : channels) c = compose(master, c.toMap());
            // A curve held level past its ends can come to more points than
            // the engine's curve takes: resampled, same shape.
            for (auto &c : channels)
                if (c.toMap().value("xs").toList().size() > 20) c = compose(identity, c.toMap());
            out.values << QVariantMap{
                {"curveset", true}, {"enabled", true}, {"linked", !coloured}, {"channels", channels}};
            used.insert(curveBase == "ToneCurvePV2012" ? "ToneCurveName2012" : "ToneCurveName");
            for (const auto &key : curveKeys) {
                used.insert(key);
                out.converted << key + " → RGB point curve (approximate; absent channels start linear"
                                       + QLatin1String(coloured && master.value("xs") != master.value("ys") ? ", the master curve folded into each colour)" : ")");
            }
        } else
            for (const auto &key : curveKeys)
                if (!used.contains(key))
                    warn(key, "not converted because its panel is disabled in the source preset");
    }
    const QSet<QString> metadata{"Name",
                                 "Group",
                                 "Description",
                                 "UUID",
                                 "PresetType",
                                 "Version",
                                 "ProcessVersion",
                                 "Cluster",
                                 "SupportsAmount",
                                 "SupportsColor",
                                 "SupportsMonochrome",
                                 "SupportsHighDynamicRange",
                                 "SupportsNormalDynamicRange",
                                 "SupportsSceneReferred",
                                 "SupportsOutputReferred",
                                 "CameraModelRestriction",
                                 "Copyright",
                                 "ContactInfo",
                                 "HasSettings",
                                 "IncrementalTemperature",
                                 "IncrementalTint",
                                 // A photo's sidecar carries these beside the settings.
                                 "AlreadyApplied",
                                 "RawFileName",
                                 "LensProfileSetup",
                                 "LensProfileName",
                                 "LensProfileFilename",
                                 "LensProfileDigest",
                                 "LensProfileIsEmbedded",
                                 "CropConstrainToWarp",
                                 "CropConstrainToUnitSquare",
                                 "ToneCurveName",
                                 "ToneCurveName2012",
                                 "OverrideLookVignette",
                                 "ShortName",
                                 "SortName"};
    // An empty Look is no look; sharpening's detail and masking at
    // The source editor's defaults change nothing.
    if (fields.value("Look").toMap().value("Name").toString().isEmpty() && fields.value("Look").toString().isEmpty()) used.insert("Look");
    if (fields.contains("SharpenDetail") && fields.value("SharpenDetail").toString().toDouble() == 25) used.insert("SharpenDetail");
    if (fields.contains("SharpenEdgeMasking") && fields.value("SharpenEdgeMasking").toString().toDouble() == 0) used.insert("SharpenEdgeMasking");
    // A camera profile other than the source editor's own (a film look's DCP) is
    // looked up by name for each photo's camera when the preset is applied,
    // in the camera profiles folder, and shown in place of the tone mapper.
    if (fields.contains("CameraProfile")) {
        const QString profile = fields.value("CameraProfile").toString().simplified();
        used.insert("CameraProfile");
        if (CameraProfiles::isBuiltIn(profile)) {
            if (!sidecar && !profile.isEmpty()) out.warnings << "CameraProfile: " + profile + " is the source editor's own; OmaRAW's camera colour is used";
        } else if (profile.size() > 256) out.error = "Invalid setting: CameraProfile";
        else {
            out.values << QVariantMap{{"op", "omarawprint"}, {"field", "cameraprofile"}, {"name", profile}};
            out.converted << "CameraProfile → camera profile \"" + profile + "\", found for each photo's camera in the camera profiles folder";
            if (CameraProfiles::folder().isEmpty())
                out.warnings << "CameraProfile: no camera profiles folder is set, so \"" + profile + "\" cannot be found yet (Adjustments ▸ Presets ▸ Camera Profiles Folder)";
        }
    }
    // Restrictions and relative WB affect applicability; report these even
    // though they are not ordinary develop slider values.
    for (const auto &key : {QStringLiteral("CameraModelRestriction"), QStringLiteral("IncrementalTemperature"),
                            QStringLiteral("IncrementalTint")})
        if (fields.contains(key) && !fields.value(key).toString().isEmpty())
            warn(key, "not translated; check this preset on the target photo");
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (used.contains(it.key()) || metadata.contains(it.key()))
            continue;
        if (it.key().startsWith("Enable")) {
            bool ok = false;
            const bool on = flag(it.value(), ok);
            if (ok && on)
                continue;
        }
        out.warnings << it.key() + ": not translated";
    }
    if (out.values.isEmpty() && out.error.isEmpty())
        out.error = sidecar ? "The XMP edit holds nothing OmaRAW can convert." : "No supported adjustments were found; nothing was imported.";
    if (!out.error.isEmpty())
        out.values.clear();
}
} // namespace

// ── the other way ──────────────────────────────────────────────────────────
// The inverse of the conversion above, for the settings it covers. Every
// covered setting is written, at the source editor's neutral value when OmaRAW's
// module is off, so a setting taken back in OmaRAW is taken back there too.
QVariantMap XmpPreset::cameraRawFrom(const QVariantList &values, const QVariantMap &curve, bool raw) {
    QHash<QString, double> v;
    QHash<QString, bool> on;
    for (const QVariant &row : values) {
        const QVariantMap m = row.toMap();
        const QString op = m.value(QStringLiteral("op")).toString(), field = m.value(QStringLiteral("field")).toString();
        if (op.isEmpty()) continue;
        if (m.contains(QStringLiteral("enabled"))) on[op] = m.value(QStringLiteral("enabled")).toBool();
        if (!field.isEmpty() && m.contains(QStringLiteral("value"))) v[op + QLatin1Char('.') + field] = m.value(QStringLiteral("value")).toDouble();
    }
    auto has = [&](const char *key) { return v.contains(QString::fromLatin1(key)); };
    auto get = [&](const char *key, double fallback = 0) { return v.value(QString::fromLatin1(key), fallback); };
    auto live = [&](const char *op) { return on.value(QString::fromLatin1(op), false); };
    auto text = [](double n, int decimals, bool sign = false) {
        const QString t = QString::number(n, 'f', decimals);
        return sign && n >= 0 ? QLatin1Char('+') + t : t;
    };
    auto slider = [&](double n, double low, double high) { return int(std::lround(std::clamp(n, low, high))); };
    QVariantMap crs;
    crs["Version"] = QStringLiteral("11.0");
    crs["ProcessVersion"] = QStringLiteral("11.0");
    crs["HasSettings"] = QStringLiteral("True");
    crs["AlreadyApplied"] = QStringLiteral("False");
    // Light
    crs["Exposure2012"] = text(std::clamp(live("exposure") ? get("exposure.exposure") - (raw ? 0.7 : 0) : 0.0, -5.0, 5.0), 2, true);
    const double contrast = live("sigmoid") && get("sigmoid.middle_grey_contrast") > 0 ? 100 * std::log2(get("sigmoid.middle_grey_contrast") / 1.5) : 0;
    crs["Contrast2012"] = QString::number(slider(contrast, -100, 100));
    crs["Highlights2012"] = QString::number(live("shadhi") ? slider(get("shadhi.highlights"), -100, 100) : 0);
    crs["Shadows2012"] = QString::number(live("shadhi") ? slider(get("shadhi.shadows"), -100, 100) : 0);
    crs["Whites2012"] = QStringLiteral("0");
    crs["Blacks2012"] = QString::number(live("exposure") ? slider(get("exposure.black") / -0.0002, -100, 100) : 0);
    crs["Clarity2012"] = QString::number(live("bilat") ? slider(get("bilat.detail") * 100, -100, 100) : 0);
    crs["Texture"] = QString::number(live("diffuse") ? slider(get("diffuse.texture"), -100, 100) : 0);
    crs["Dehaze"] = QString::number(live("hazeremoval") ? slider(get("hazeremoval.strength") * 100, -100, 100) : 0);
    // Colour
    if (raw && live("channelmixerrgb") && has("channelmixerrgb.temperature")) {
        crs["WhiteBalance"] = QStringLiteral("Custom");
        crs["Temperature"] = QString::number(slider(get("channelmixerrgb.temperature"), 2000, 50000));
        crs["Tint"] = QString::number(slider(get("channelmixerrgb.tint") * 150 / 100, -150, 150));
    } else if (raw) crs["WhiteBalance"] = QStringLiteral("As Shot");
    const bool colour = live("colorbalancergb");
    const bool mono = live("monochrome");
    const bool grey = mono || (colour && get("colorbalancergb.saturation_global") <= -0.999 && get("colorbalancergb.chroma_global") <= -0.999);
    crs["ConvertToGrayscale"] = grey ? QStringLiteral("True") : QStringLiteral("False");
    crs["Saturation"] = QString::number(colour && !grey ? slider(get("colorbalancergb.saturation_global") * 100, -100, 100) : 0);
    crs["Vibrance"] = QString::number(colour && !grey ? slider(get("colorbalancergb.vibrance") * 100, -100, 100) : 0);
    const char *lrBands[] = {"Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"};
    const char *omaBands[] = {"red", "orange", "yellow", "green", "cyan", "blue", "lavender", "magenta"};
    const char *monoBands[] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};
    const bool mixer = live("colorequal");
    for (int i = 0; i < 8; ++i) {
        const QString band = QString::fromLatin1(omaBands[i]), lr = QString::fromLatin1(lrBands[i]);
        crs["HueAdjustment" + lr] = QString::number(mixer ? slider(v.value("colorequal.hue_" + band) / .3, -100, 100) : 0);
        crs["SaturationAdjustment" + lr] = QString::number(mixer ? slider((v.value("colorequal.sat_" + band, 1) - 1) * 100, -100, 100) : 0);
        crs["LuminanceAdjustment" + lr] = QString::number(mixer ? slider((v.value("colorequal.bright_" + band, 1) - 1) * 100, -100, 100) : 0);
        crs["GrayMixer" + lr] = QString::number(mono ? slider(v.value("monochrome.mix_" + QString::fromLatin1(monoBands[i])) * 100, -100, 100) : 0);
    }
    // Detail and effects
    crs["Sharpness"] = QString::number(live("sharpen") ? slider(get("sharpen.amount") / 0.02, 0, 150) : 0);
    crs["SharpenRadius"] = text(live("sharpen") ? std::clamp(get("sharpen.radius", 1), 0.5, 3.0) : 1.0, 1, true);
    const bool nr = live("denoiseprofile") && std::lround(get("denoiseprofile.mode")) == 3;
    crs["LuminanceSmoothing"] = QString::number(nr ? slider(get("denoiseprofile.strength") * 50, 0, 100) : 0);
    if (nr) crs["LuminanceNoiseReductionDetail"] = QString::number(slider((get("denoiseprofile.central_pixel_weight", 1.55) - 0.1) / 0.029, 0, 100));
    crs["GrainAmount"] = QString::number(live("grain") ? slider(get("grain.strength"), 0, 100) : 0);
    crs["PostCropVignetteAmount"] = QString::number(live("vignette") ? slider(get("vignette.brightness") * 100, -100, 100) : 0);
    crs["LensProfileEnable"] = live("lens") ? QStringLiteral("1") : QStringLiteral("0");
    // Crop: a turned one (the straighten tool) has no XMP equivalent here.
    const bool turned = live("ashift") && qAbs(get("ashift.rotation")) > 0.01;
    if (live("crop") && !turned && has("crop.cw")) {
        crs["HasCrop"] = QStringLiteral("True");
        crs["CropLeft"] = text(get("crop.cx"), 6); crs["CropTop"] = text(get("crop.cy"), 6);
        crs["CropRight"] = text(get("crop.cw", 1), 6); crs["CropBottom"] = text(get("crop.ch", 1), 6);
        crs["CropAngle"] = QStringLiteral("0");
    } else crs["HasCrop"] = QStringLiteral("False");
    // The point curve, when its three channels are one (the source editor's master).
    QStringList points{QStringLiteral("0, 0"), QStringLiteral("255, 255")};
    QString curveName = QStringLiteral("Linear");
    const QVariantList channels = curve.value(QStringLiteral("channels")).toList();
    if (curve.value(QStringLiteral("found")).toBool() && curve.value(QStringLiteral("enabled")).toBool()
        && curve.value(QStringLiteral("linked"), true).toBool() && !channels.isEmpty()) {
        const QVariantMap c = channels.first().toMap();
        const QVariantList xs = c.value(QStringLiteral("xs")).toList(), ys = c.value(QStringLiteral("ys")).toList();
        if (xs.size() >= 2 && xs.size() == ys.size()) {
            QStringList p;
            int previous = -1;
            for (int i = 0; i < xs.size(); ++i) {
                const int x = slider(xs[i].toDouble() * 255, 0, 255), y = slider(ys[i].toDouble() * 255, 0, 255);
                if (x <= previous) continue;
                p << QStringLiteral("%1, %2").arg(x).arg(y);
                previous = x;
            }
            if (p.size() >= 2) { points = p; curveName = QStringLiteral("Custom"); }
        }
    }
    crs["ToneCurveName2012"] = curveName;
    crs["ToneCurvePV2012"] = points;
    return crs;
}
