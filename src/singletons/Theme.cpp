// SPDX-FileCopyrightText: 2016 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Theme.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"
#include "common/QLogging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Resources.hpp"
#include "singletons/WindowManager.hpp"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#    include <QStyleHints>
#endif
#include <QApplication>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

using namespace chatterino;
using namespace literals;

void parseInto(const QJsonObject &obj, const QJsonObject &fallbackObj,
               QLatin1String key, QColor &color)
{
    auto parseColorFrom = [](const auto &obj,
                             QLatin1String key) -> std::optional<QColor> {
        auto jsonValue = obj[key];
        if (!jsonValue.isString()) [[unlikely]]
        {
            return std::nullopt;
        }
        QColor parsed = {jsonValue.toString()};
        if (!parsed.isValid()) [[unlikely]]
        {
            qCWarning(chatterinoTheme).nospace()
                << "While parsing " << key << ": '" << jsonValue.toString()
                << "' isn't a valid color.";
            return std::nullopt;
        }
        return parsed;
    };

    auto firstColor = parseColorFrom(obj, key);
    if (firstColor.has_value())
    {
        color = firstColor.value();
        return;
    }

    if (!fallbackObj.isEmpty())
    {
        auto fallbackColor = parseColorFrom(fallbackObj, key);
        if (fallbackColor.has_value())
        {
            color = fallbackColor.value();
            return;
        }
    }

    qCWarning(chatterinoTheme) << key
                               << "was expected but not found in the "
                                  "current theme, and no fallback value found.";
}

#define _c2StringLit(s, ty) s##ty
#define parseColor(to, from, key) \
    parseInto(from, from##Fallback, _c2StringLit(#key, _L1), (to).from.key)

void parseWindow(const QJsonObject &window, const QJsonObject &windowFallback,
                 chatterino::Theme &theme)
{
    parseColor(theme, window, background);
    parseColor(theme, window, text);
}

void parseTabs(const QJsonObject &tabs, const QJsonObject &tabsFallback,
               chatterino::Theme &theme)
{
    const auto parseTabColors = [](const auto &json, const auto &jsonFallback,
                                   auto &tab) {
        parseInto(json, jsonFallback, "text"_L1, tab.text);
        {
            const auto backgrounds = json["backgrounds"_L1].toObject();
            const auto backgroundsFallback =
                jsonFallback["backgrounds"_L1].toObject();
            parseColor(tab, backgrounds, regular);
            parseColor(tab, backgrounds, hover);
            parseColor(tab, backgrounds, unfocused);
        }
        {
            const auto line = json["line"_L1].toObject();
            const auto lineFallback = jsonFallback["line"_L1].toObject();
            parseColor(tab, line, regular);
            parseColor(tab, line, hover);
            parseColor(tab, line, unfocused);
        }
    };
    parseColor(theme, tabs, dividerLine);
    parseColor(theme, tabs, liveIndicator);
    parseColor(theme, tabs, rerunIndicator);
    parseTabColors(tabs["regular"_L1].toObject(),
                   tabsFallback["regular"_L1].toObject(), theme.tabs.regular);
    parseTabColors(tabs["newMessage"_L1].toObject(),
                   tabsFallback["newMessage"_L1].toObject(),
                   theme.tabs.newMessage);
    parseTabColors(tabs["highlighted"_L1].toObject(),
                   tabsFallback["highlighted"_L1].toObject(),
                   theme.tabs.highlighted);
    parseTabColors(tabs["selected"_L1].toObject(),
                   tabsFallback["selected"_L1].toObject(), theme.tabs.selected);
}

void parseTextColors(const QJsonObject &textColors,
                     const QJsonObject &textColorsFallback, auto &messages)
{
    parseColor(messages, textColors, regular);
    parseColor(messages, textColors, caret);
    parseColor(messages, textColors, link);
    parseColor(messages, textColors, system);
    parseColor(messages, textColors, chatPlaceholder);
}

void parseMessageBackgrounds(const QJsonObject &backgrounds,
                             const QJsonObject &backgroundsFallback,
                             auto &messages)
{
    parseColor(messages, backgrounds, regular);
    parseColor(messages, backgrounds, alternate);
}

void parseMessages(const QJsonObject &messages,
                   const QJsonObject &messagesFallback,
                   chatterino::Theme &theme)
{
    parseTextColors(messages["textColors"_L1].toObject(),
                    messagesFallback["textColors"_L1].toObject(),
                    theme.messages);
    parseMessageBackgrounds(messages["backgrounds"_L1].toObject(),
                            messagesFallback["backgrounds"_L1].toObject(),
                            theme.messages);
    parseColor(theme, messages, disabled);
    parseColor(theme, messages, selection);
    parseColor(theme, messages, highlightAnimationStart);
    parseColor(theme, messages, highlightAnimationEnd);
}

void parseOverlayMessages(const QJsonObject &overlayMessages,
                          const QJsonObject &overlayMessagesFallback,
                          chatterino::Theme &theme)
{
    parseTextColors(overlayMessages["textColors"_L1].toObject(),
                    overlayMessagesFallback["textColors"_L1].toObject(),
                    theme.overlayMessages);
    parseMessageBackgrounds(
        overlayMessages["backgrounds"_L1].toObject(),
        overlayMessagesFallback["backgrounds"_L1].toObject(),
        theme.overlayMessages);
    parseColor(theme, overlayMessages, disabled);
    parseColor(theme, overlayMessages, selection);
    parseColor(theme, overlayMessages, background);
}

void parseScrollbars(const QJsonObject &scrollbars,
                     const QJsonObject &scrollbarsFallback,
                     chatterino::Theme &theme)
{
    parseColor(theme, scrollbars, background);
    parseColor(theme, scrollbars, thumb);
    parseColor(theme, scrollbars, thumbSelected);
}

void parseSplits(const QJsonObject &splits, const QJsonObject &splitsFallback,
                 chatterino::Theme &theme)
{
    parseColor(theme, splits, messageSeperator);
    parseColor(theme, splits, background);
    parseColor(theme, splits, dropPreview);
    parseColor(theme, splits, dropPreviewBorder);
    parseColor(theme, splits, dropTargetRect);
    parseColor(theme, splits, dropTargetRectBorder);
    parseColor(theme, splits, resizeHandle);
    parseColor(theme, splits, resizeHandleBackground);

    {
        const auto header = splits["header"_L1].toObject();
        const auto headerFallback = splitsFallback["header"_L1].toObject();
        parseColor(theme.splits, header, border);
        parseColor(theme.splits, header, focusedBorder);
        parseColor(theme.splits, header, background);
        parseColor(theme.splits, header, focusedBackground);
        parseColor(theme.splits, header, text);
        parseColor(theme.splits, header, focusedText);
    }
    {
        const auto input = splits["input"_L1].toObject();
        const auto inputFallback = splitsFallback["input"_L1].toObject();
        parseColor(theme.splits, input, background);
        parseColor(theme.splits, input, backgroundPulse);
        parseColor(theme.splits, input, text);
    }
}

void parseColors(const QJsonObject &root, const QJsonObject &fallbackTheme,
                 chatterino::Theme &theme)
{
    const auto colors = root["colors"_L1].toObject();
    const auto fallbackColors = fallbackTheme["colors"_L1].toObject();

    parseInto(colors, fallbackColors, "accent"_L1, theme.accent);

    parseWindow(colors["window"_L1].toObject(),
                fallbackColors["window"_L1].toObject(), theme);
    parseTabs(colors["tabs"_L1].toObject(),
              fallbackColors["tabs"_L1].toObject(), theme);
    parseMessages(colors["messages"_L1].toObject(),
                  fallbackColors["messages"_L1].toObject(), theme);
    parseOverlayMessages(colors["overlayMessages"_L1].toObject(),
                         fallbackColors["overlayMessages"_L1].toObject(),
                         theme);
    parseScrollbars(colors["scrollbars"_L1].toObject(),
                    fallbackColors["scrollbars"_L1].toObject(), theme);
    parseSplits(colors["splits"_L1].toObject(),
                fallbackColors["splits"_L1].toObject(), theme);
}
#undef parseColor
#undef _c2StringLit

std::optional<QJsonObject> loadThemeFromPath(const QString &path)
{
    QFile file(path);
    if (!file.open(QFile::ReadOnly))
    {
        qCWarning(chatterinoTheme)
            << "Failed to open" << file.fileName() << "at" << path;
        return std::nullopt;
    }

    QJsonParseError error{};
    auto json = QJsonDocument::fromJson(file.readAll(), &error);
    if (!json.isObject())
    {
        qCWarning(chatterinoTheme) << "Failed to parse" << file.fileName()
                                   << "error:" << error.errorString();
        return std::nullopt;
    }

    return json.object();
}

std::optional<QJsonObject> loadTheme(const ThemeDescriptor &theme)
{
    return loadThemeFromPath(theme.path);
}

QColor blendColors(const QColor &from, const QColor &to, qreal amount)
{
    const auto clamped = std::clamp(amount, 0.0, 1.0);
    const auto blend = [clamped](int a, int b) {
        return qRound(a + (b - a) * clamped);
    };

    return QColor(blend(from.red(), to.red()), blend(from.green(), to.green()),
                  blend(from.blue(), to.blue()), blend(from.alpha(), to.alpha()));
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

QString cssColor(const QColor &color)
{
    return color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
}

void applyNativePalette(chatterino::Theme &theme)
{
    const auto palette = QApplication::palette();
    const auto window = palette.color(QPalette::Window);
    const auto windowText = palette.color(QPalette::WindowText);
    const auto base = palette.color(QPalette::Base);
    const auto alternateBase = palette.color(QPalette::AlternateBase);
    const auto button = palette.color(QPalette::Button);
    const auto buttonText = palette.color(QPalette::ButtonText);
    const auto text = palette.color(QPalette::Text);
    const auto disabledText = palette.color(QPalette::Disabled, QPalette::Text);
    const auto highlight = palette.color(QPalette::Highlight);
    const auto highlightedText = palette.color(QPalette::HighlightedText);
    const auto link = palette.color(QPalette::Link);
    const auto placeholder = palette.color(QPalette::PlaceholderText);
    const auto mid = palette.color(QPalette::Mid);
    const bool light = window.lightness() >= 128;
    const auto hover = light ? blendColors(button, Qt::black, 0.06)
                             : blendColors(button, Qt::white, 0.08);
    const auto focused = blendColors(button, highlight, light ? 0.18 : 0.28);
    const auto separator = light ? blendColors(window, Qt::black, 0.18)
                                 : blendColors(window, Qt::white, 0.18);
    const auto pulse = blendColors(base, highlight, light ? 0.15 : 0.25);

    theme.isLight_ = light;
    theme.palette = palette;
    theme.accent = highlight;

    theme.window.background = window;
    theme.window.text = windowText;

    theme.tabs.dividerLine = separator;
    theme.tabs.liveIndicator = highlight;
    theme.tabs.rerunIndicator = link;

    auto setTab = [&](auto &tab, const QColor &background) {
        tab.text = buttonText;
        tab.backgrounds.regular = background;
        tab.backgrounds.hover = hover;
        tab.backgrounds.unfocused = window;
        tab.line.regular = separator;
        tab.line.hover = highlight;
        tab.line.unfocused = mid;
    };
    setTab(theme.tabs.regular, button);
    setTab(theme.tabs.newMessage, blendColors(button, highlight, light ? 0.10 : 0.18));
    setTab(theme.tabs.highlighted, blendColors(button, highlight, light ? 0.18 : 0.28));
    theme.tabs.selected.text = highlightedText;
    theme.tabs.selected.backgrounds.regular = highlight;
    theme.tabs.selected.backgrounds.hover = blendColors(highlight, windowText, 0.12);
    theme.tabs.selected.backgrounds.unfocused = blendColors(button, highlight, 0.35);
    theme.tabs.selected.line.regular = highlight;
    theme.tabs.selected.line.hover = highlight;
    theme.tabs.selected.line.unfocused = mid;

    auto setMessageColors = [&](auto &messages) {
        messages.textColors.regular = text;
        messages.textColors.caret = text;
        messages.textColors.link = link;
        messages.textColors.system = placeholder.isValid() ? placeholder : disabledText;
        messages.textColors.chatPlaceholder = placeholder.isValid() ? placeholder : disabledText;
        messages.backgrounds.regular = base;
        messages.backgrounds.alternate = alternateBase;
        messages.disabled = disabledText;
        messages.selection = highlight;
    };
    setMessageColors(theme.messages);
    theme.messages.highlightAnimationStart = withAlpha(highlight, 90);
    theme.messages.highlightAnimationEnd = withAlpha(highlight, 0);
    setMessageColors(theme.overlayMessages);
    theme.overlayMessages.background = withAlpha(window, 230);

    theme.scrollbars.background = window;
    theme.scrollbars.thumb = mid;
    theme.scrollbars.thumbSelected = highlight;

    theme.splits.messageSeperator = separator;
    theme.splits.background = window;
    theme.splits.dropPreview = withAlpha(highlight, 80);
    theme.splits.dropPreviewBorder = highlight;
    theme.splits.dropTargetRect = withAlpha(highlight, 45);
    theme.splits.dropTargetRectBorder = highlight;
    theme.splits.resizeHandle = mid;
    theme.splits.resizeHandleBackground = window;

    theme.splits.header.border = separator;
    theme.splits.header.focusedBorder = highlight;
    theme.splits.header.background = button;
    theme.splits.header.focusedBackground = focused;
    theme.splits.header.text = buttonText;
    theme.splits.header.focusedText = buttonText;

    theme.splits.input.background = base;
    theme.splits.input.backgroundPulse = pulse;
    theme.splits.input.text = text;

    theme.splits.input.styleSheet = uR"(
        background: %1;
        border: 1px solid %2;
        color: %3;
        selection-background-color: %4;
        selection-color: %5;
    )"_s.arg(cssColor(theme.splits.input.background),
              cssColor(theme.tabs.selected.backgrounds.regular),
              cssColor(theme.splits.input.text), cssColor(highlight),
              cssColor(highlightedText));

    theme.buttons.copy = light ? getResources().buttons.copyDark
                               : getResources().buttons.copyLight;
}

}  // namespace

namespace chatterino {

const std::vector<ThemeDescriptor> Theme::builtInThemes{
    {
        .key = "White",
        .path = ":/themes/White.json",
        .name = "White",
    },
    {
        .key = "Light",
        .path = ":/themes/Light.json",
        .name = "Light",
    },
    {
        .key = "Dark",
        .path = ":/themes/Dark.json",
        .name = "Dark",
    },
    {
        .key = "Black",
        .path = ":/themes/Black.json",
        .name = "Black",
    },
    {
        .key = "Bloody-Mary",
        .path = ":/themes/Bloody-Mary.json",
        .name = "Bloody Mary",
    },
    {
        .key = "Blue-Steel",
        .path = ":/themes/Blue-Steel.json",
        .name = "Blue Steel",
    },
    {
        .key = "Chocolate",
        .path = ":/themes/Chocolate.json",
        .name = "Chocolate",
    },
    {
        .key = "Cobalt",
        .path = ":/themes/Cobalt.json",
        .name = "Cobalt",
    },
    {
        .key = "Dark-Mint",
        .path = ":/themes/Dark-Mint.json",
        .name = "Dark Mint",
    },
    {
        .key = "Deep-Blue",
        .path = ":/themes/Deep-Blue.json",
        .name = "Deep Blue",
    },
    {
        .key = "Goth",
        .path = ":/themes/Goth.json",
        .name = "Goth",
    },
    {
        .key = "New-Black",
        .path = ":/themes/New-Black.json",
        .name = "New Black",
    },
    {
        .key = "New-Dark",
        .path = ":/themes/New-Dark.json",
        .name = "New Dark",
    },
    {
        .key = "Purple",
        .path = ":/themes/Purple.json",
        .name = "Purple",
    },
    {
        .key = "Rose",
        .path = ":/themes/Rose.json",
        .name = "Rose",
    },
    {
        .key = "Tokyo-Night",
        .path = ":/themes/Tokyo-Night.json",
        .name = "Tokyo Night",
    },
    {
        .key = "Wine",
        .path = ":/themes/Wine.json",
        .name = "Wine",
    },
};

const ThemeDescriptor Theme::fallbackTheme = Theme::builtInThemes.at(2);

bool Theme::isLightTheme() const
{
    return this->isLight_;
}

bool Theme::isSystemTheme() const
{
    return this->themeName == u"System"_s;
}

Theme::Theme(const Paths &paths)
{
    this->themeName.connect(
        [this](auto themeName) {
            qCInfo(chatterinoTheme) << "Theme updated to" << themeName;
            this->update();
        },
        false);
    auto updateIfSystem = [this](const auto &) {
        if (this->isSystemTheme())
        {
            this->update();
        }
    };
    this->darkSystemThemeName.connect(updateIfSystem, false);
    this->lightSystemThemeName.connect(updateIfSystem, false);

    this->loadAvailableThemes(paths);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(QApplication::styleHints(),
                     &QStyleHints::colorSchemeChanged, &this->lifetime_,
                     [this] {
                         if (this->isSystemTheme())
                         {
                             this->update();
                             getApp()->getWindows()->forceLayoutChannelViews();
                         }
                     });
#endif
    this->update();
}

void Theme::update()
{
    constexpr const double nsToMs = 1.0 / 1000000.0;
    QElapsedTimer timer;
    timer.start();

    if (this->isSystemTheme())
    {
        applyNativePalette(*this);
        auto parseTs = double(timer.nsecsElapsed()) * nsToMs;

        this->updated.invoke();
        auto updateTs = double(timer.nsecsElapsed()) * nsToMs;
        qCDebug(chatterinoTheme).nospace().noquote()
            << "Updated native Qt theme in " << QString::number(updateTs, 'f', 2)
            << "ms (parse: " << QString::number(parseTs, 'f', 2)
            << "ms, update: " << QString::number(updateTs - parseTs, 'f', 2)
            << "ms)";
        return;
    }

    auto oTheme = this->findThemeByKey(this->themeName);

    std::optional<QJsonObject> themeJSON;
    QString themePath;
    bool isCustomTheme = false;
    if (!oTheme)
    {
        qCWarning(chatterinoTheme)
            << "Theme" << this->themeName
            << "not found, falling back to the fallback theme";

        themeJSON = loadTheme(fallbackTheme);
        themePath = fallbackTheme.path;
    }
    else
    {
        const auto &theme = *oTheme;

        themeJSON = loadTheme(theme);
        themePath = theme.path;

        if (!themeJSON)
        {
            qCWarning(chatterinoTheme)
                << "Theme" << this->themeName
                << "not valid, falling back to the fallback theme";

            themeJSON = loadTheme(fallbackTheme);
            themePath = fallbackTheme.path;
        }
        else
        {
            isCustomTheme = theme.custom;
        }
    }
    auto loadTs = double(timer.nsecsElapsed()) * nsToMs;

    if (!themeJSON)
    {
        qCWarning(chatterinoTheme)
            << "Failed to load" << this->themeName << "or the fallback theme";
        return;
    }

    if (this->isAutoReloading() && this->currentThemeJson_ == *themeJSON)
    {
        return;
    }

    this->parseFrom(*themeJSON, isCustomTheme);
    this->currentThemePath_ = themePath;

    auto parseTs = double(timer.nsecsElapsed()) * nsToMs;

    this->updated.invoke();
    auto updateTs = double(timer.nsecsElapsed()) * nsToMs;
    qCDebug(chatterinoTheme).nospace().noquote()
        << "Updated theme in " << QString::number(updateTs, 'f', 2)
        << "ms (load: " << QString::number(loadTs, 'f', 2)
        << "ms, parse: " << QString::number(parseTs - loadTs, 'f', 2)
        << "ms, update: " << QString::number(updateTs - parseTs, 'f', 2)
        << "ms)";

    if (this->isAutoReloading())
    {
        this->currentThemeJson_ = *themeJSON;
    }
}

std::vector<std::pair<QString, QVariant>> Theme::availableThemes() const
{
    std::vector<std::pair<QString, QVariant>> packagedThemes;

    for (const auto &theme : this->availableThemes_)
    {
        if (theme.custom)
        {
            auto p = std::make_pair(
                QStringLiteral("Custom: %1").arg(theme.name), theme.key);

            packagedThemes.emplace_back(p);
        }
        else
        {
            auto p = std::make_pair(theme.name, theme.key);

            packagedThemes.emplace_back(p);
        }
    }

    return packagedThemes;
}

void Theme::loadAvailableThemes(const Paths &paths)
{
    this->availableThemes_ = Theme::builtInThemes;

    auto dir = QDir(paths.themesDirectory);
    for (const auto &info :
         dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name))
    {
        if (!info.isFile())
        {
            continue;
        }

        if (!info.fileName().endsWith(".json"))
        {
            continue;
        }

        auto themeName = info.baseName();

        auto themeDescriptor = ThemeDescriptor{
            info.fileName(), info.absoluteFilePath(), themeName, true};

        auto theme = loadTheme(themeDescriptor);
        if (!theme)
        {
            qCWarning(chatterinoTheme) << "Failed to parse theme at" << info;
            continue;
        }

        this->availableThemes_.emplace_back(std::move(themeDescriptor));
    }
}

std::optional<ThemeDescriptor> Theme::findThemeByKey(const QString &key)
{
    for (const auto &theme : this->availableThemes_)
    {
        if (theme.key == key)
        {
            return theme;
        }
    }

    return std::nullopt;
}

void Theme::parseFrom(const QJsonObject &root, bool isCustomTheme)
{
    this->isLight_ =
        root["metadata"_L1]["iconTheme"_L1].toString() == u"dark"_s;

    std::optional<QJsonObject> fallbackTheme;
    if (isCustomTheme)
    {
        auto fallbackThemeName =
            root["metadata"_L1]["fallbackTheme"_L1].toString(
                this->isLightTheme() ? "Light" : "Dark");
        for (const auto &theme : Theme::builtInThemes)
        {
            if (fallbackThemeName.compare(theme.key, Qt::CaseInsensitive) == 0)
            {
                fallbackTheme = loadTheme(theme);
                break;
            }
        }
    }

    parseColors(root, fallbackTheme.value_or(QJsonObject()), *this);

    this->splits.input.styleSheet = uR"(
        background: %1;
        border: %2;
        color: %3;
        selection-background-color: %4;
    )"_s.arg(
        this->splits.input.background.name(QColor::HexArgb),
        this->tabs.selected.backgrounds.regular.name(QColor::HexArgb),
        this->messages.textColors.regular.name(QColor::HexArgb),
        this->isLightTheme()
            ? u"#68B1FF"_s
            : this->tabs.selected.backgrounds.regular.name(QColor::HexArgb));

    if (this->isLightTheme())
    {
        this->buttons.copy = getResources().buttons.copyDark;
    }
    else
    {
        this->buttons.copy = getResources().buttons.copyLight;
    }

    auto palette = QApplication::palette();

    if (this->isLightTheme())
    {
        palette.setColor(QPalette::Window, this->window.background);
        palette.setColor(QPalette::Base, {0xe9, 0xe9, 0xe9});
        palette.setColor(QPalette::AlternateBase, {0xe0, 0xe0, 0xe0});
        palette.setColor(QPalette::Button, {0xd9, 0xd9, 0xd9});
        palette.setColor(QPalette::Text, this->window.text);
        palette.setColor(QPalette::WindowText, this->window.text);
        palette.setColor(QPalette::ButtonText, this->window.text);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, Qt::black);
        palette.setColor(QPalette::Link, Qt::blue);
        palette.setColor(QPalette::LinkVisited, Qt::magenta);
        palette.setColor(QPalette::Highlight, {42, 130, 218});
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::PlaceholderText, {0x90, 0x90, 0x90});
    }
    this->palette = palette;
}

bool Theme::isAutoReloading() const
{
    return this->themeReloadTimer_ != nullptr;
}

void Theme::setAutoReload(bool autoReload)
{
    if (autoReload == this->isAutoReloading())
    {
        return;
    }

    if (!autoReload)
    {
        this->themeReloadTimer_.reset();
        this->currentThemeJson_ = {};
        return;
    }

    this->themeReloadTimer_ = std::make_unique<QTimer>();
    QObject::connect(this->themeReloadTimer_.get(), &QTimer::timeout, [this]() {
        this->update();
    });
    this->themeReloadTimer_->setInterval(Theme::AUTO_RELOAD_INTERVAL_MS);
    this->themeReloadTimer_->start();

    qCDebug(chatterinoTheme) << "Enabled theme watcher";
}

void Theme::normalizeColor(QColor &color) const
{
    if (this->isLightTheme())
    {
        if (color.lightnessF() > 0.5)
        {
            color.setHslF(color.hueF(), color.saturationF(), 0.5);
        }

        if (color.lightnessF() > 0.4 && color.hueF() > 0.1 &&
            color.hueF() < 0.33333)
        {
            color.setHslF(
                color.hueF(), color.saturationF(),
                color.lightnessF() - sin((color.hueF() - 0.1) / (0.3333 - 0.1) *
                                         std::numbers::pi) *
                                         color.saturationF() * 0.4);
        }
    }
    else
    {
        if (color.lightnessF() < 0.5)
        {
            color.setHslF(color.hueF(), color.saturationF(), 0.5);
        }

        if (color.lightnessF() < 0.6 && color.hueF() > 0.54444 &&
            color.hueF() < 0.83333)
        {
            color.setHslF(color.hueF(), color.saturationF(),
                          color.lightnessF() +
                              sin((color.hueF() - 0.54444) /
                                  (0.8333 - 0.54444) * std::numbers::pi) *
                                  color.saturationF() * 0.4);
        }
    }
}

Theme *getTheme()
{
    return getApp()->getThemes();
}

}  // namespace chatterino
