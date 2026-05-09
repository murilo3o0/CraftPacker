#pragma once
#include <QString>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <algorithm>

// Phase 1: The "Ignore Brackets" feature
// Strips parentheses, brackets, braces AND their contents from search strings
namespace StringCleaner {

// Known alias table: display name -> stable search slug
// This fixes "Canvas Renderer" -> "canvas", "Architectury API" -> "architectury", etc.
inline QHash<QString, QString> aliasTable() {
    static QHash<QString, QString> table;
    if (table.isEmpty()) {
        table["canvas renderer"] = "canvas";
        table["architectury api"] = "architectury";
        table["architectury"] = "architectury-api";
        table["cloth config api"] = "cloth-config";
        table["cloth config"] = "cloth-config";
        table["fabric api"] = "fabric-api";
        table["rei"] = "roughly-enough-items";
        table["roughly enough items"] = "roughly-enough-items";
        table["jei"] = "just-enough-items";
        table["just enough items"] = "just-enough-items";
        table["emi"] = "emi";
        table["sodium extra"] = "sodium-extra";
        table["indium"] = "indium";
        table["mod menu"] = "modmenu";
        table["lazy dfu"] = "lazy-dfu";
        table["betterfpsdist"] = "betterfpsdistance";
        table["better fps distance"] = "betterfpsdistance";
        table["better fps dist"] = "betterfpsdistance";
        table["optifine"] = "optifabric";  // closest match
        table["sodium"] = "sodium";
        table["lithium"] = "lithium";
        table["phosphor"] = "phosphor";
        table["iris"] = "iris";
        table["shaders"] = "iris";
        table["continuity"] = "continuity";
        table["entityculling"] = "entityculling";
        table["entity culling"] = "entityculling";
        table["dynamic fps"] = "dynamic-fps";
        table["dynamicfps"] = "dynamic-fps";
        table["better biome blend"] = "better-biome-blend";
        table["betterbiomeblend"] = "better-biome-blend";
        table["starlight"] = "starlight";
        table["ferritecore"] = "ferritecore";
        table["ferrite core"] = "ferritecore";
        table["krypton"] = "krypton";
        table["languagereload"] = "languagereload";
        table["language reload"] = "languagereload";
        table["connectivity"] = "connectivity";
        table["spark"] = "spark";
        table["no chat reports"] = "no-chat-reports";
        table["nochatreports"] = "no-chat-reports";
        table["memoryleakfix"] = "memoryleakfix";
        table["memory leak fix"] = "memoryleakfix";
        table["modernfix"] = "modernfix";
        table["modern fix"] = "modernfix";
        table["cull leaves"] = "cull-leaves";
        table["cullleaves"] = "cull-leaves";
        table["immediatelyfast"] = "immediatelyfast";
        table["immediately fast"] = "immediatelyfast";
        table["reeses sodium options"] = "reeses-sodium-options";
        table["reesessodiumoptions"] = "reeses-sodium-options";
        table["distant horizons"] = "distanthorizons";
        table["distanthorizons"] = "distanthorizons";
        table["terralith"] = "terralith";
        table["byg"] = "byg";
        table["oh the biomes you'll go"] = "byg";
        table["oh the biomes you will go"] = "byg";
        table["biomes o plenty"] = "biomes-o-plenty";
        table["bop"] = "biomes-o-plenty";
        table["create"] = "create";
        table["create fabric"] = "create-fabric";
        table["applied energistics 2"] = "ae2";
        table["applied energistics"] = "ae2";
        table["ae2"] = "ae2";
        table["appleskin"] = "appleskin";
        table["apple skin"] = "appleskin";
        table["jade"] = "jade";
        table["jade 🔍"] = "jade";
        table["emf"] = "entity-model-features";
        table["entity model features"] = "entity-model-features";
        table["etf"] = "entity-texture-features";
        table["entity texture features"] = "entity-texture-features";
        table["xaeros minimap"] = "xaeros-minimap";
        table["xaero minimap"] = "xaeros-minimap";
        table["xeros minimap"] = "xaeros-minimap";
        table["xaeros world map"] = "xaeros-world-map";
        table["xaero world map"] = "xaeros-world-map";
        table["xeros world map"] = "xaeros-world-map";
        table["xaeros waypoints"] = "xaeros-world-map";
        table["xaero waypoints"] = "xaeros-world-map";
        table["yungs api"] = "yungs-api";
        table["yung's api"] = "yungs-api";
        table["yungs better dungeons"] = "yungs-better-dungeons";
        table["yung's better dungeons"] = "yungs-better-dungeons";
        table["yungs better mineshafts"] = "yungs-better-mineshafts";
        table["yung's better mineshafts"] = "yungs-better-mineshafts";
        table["yungs better strongholds"] = "yungs-better-strongholds";
        table["yungs better oceans"] = "yungs-better-oceans";
        table["yungs better nether"] = "yungs-better-nether";
        table["yungs better end"] = "yungs-better-end-island";
        table["yungs better witch huts"] = "yungs-better-witch-huts";
        table["yungs better jungle temples"] = "yungs-better-jungle-temples";
        table["yungs better desert temples"] = "yungs-better-desert-temples";
        table["yungs bridge"] = "yungs-bridge";
        table["cull leaves"] = "cull-leaves";
        table["cullleaves"] = "cull-leaves";
        table["reeses sodium options"] = "reeses-sodium-options";
        table["reeses sodium"] = "reeses-sodium-options";
        table["reesessodiumoptions"] = "reeses-sodium-options";
        table["noisium"] = "noisium";
        table["continuity"] = "continuity";
        table["entityculling"] = "entityculling";
        table["entity culling"] = "entityculling";
        table["betterfpsdist"] = "betterfpsdistance";
        table["better fps distance"] = "betterfpsdistance";
        table["fps distance"] = "betterfpsdistance";
        table["better b iome blend"] = "better-biome-blend";
        table["yungs"] = "yungs-api";
        table["yungs-extras"] = "yungs-api";
        table["bridge"] = "yungs-bridge";
        table["campanion"] = "campanion";
        table["carry on"] = "carry-on";
        table["carryon"] = "carry-on";
        table["cherished worlds"] = "cherished-worlds";
        table["chest tracker"] = "chest-tracker";
        table["clean cut"] = "clean-cut";
        table["clear despawn"] = "clear-despawn";
        table["clickthrough"] = "clickthrough";
        table["click through"] = "clickthrough";
        table["clientcraft"] = "clientcraft";
        table["client craft"] = "clientcraft";
        table["coords hud"] = "coords-hud";
        table["coordshud"] = "coords-hud";
        table["crawl"] = "crawl";
        table["create additions"] = "create-additions";
        table["create aditions"] = "create-additions";
        table["create addition"] = "create-additions";
        table["create central kitchen"] = "create-central-kitchen";
        table["cupboard"] = "cupboard";
        table["datamancer"] = "datamancer";
        table["data pack features"] = "data-pack-features";
        table["debugify"] = "debugify";
        table["deep dark regrowth"] = "deep-dark-regrowth";
        table["dusk"] = "dusk";
        table["dusk mod"] = "dusk";
        table["easiervillagertrading"] = "easiervillagertrading";
        table["easier villager trading"] = "easiervillagertrading";
        table["easy anvils"] = "easy-anvils";
        table["easy magic"] = "easy-magic";
        table["easyanvils"] = "easy-anvils";
        table["easymagic"] = "easy-magic";
        table["eating animation"] = "eating-animation";
        table["eatinganimation"] = "eating-animation";
        table["eating animation v2"] = "eating-animation";
        table["ecotones"] = "ecotones";
        table["emotecraft"] = "emotecraft";
        table["emote craft"] = "emotecraft";
        table["end remastered"] = "end-remastered";
        table["endremastered"] = "end-remastered";
        table["endrem"] = "end-remastered";
        table["enhanced block entities"] = "enhanced-block-entities";
        table["enhancedblockentities"] = "enhanced-block-entities";
        table["entity distance"] = "entity-distance";
        table["entitycollisionfix"] = "entitycollisionfix";
        table["entity collision fix"] = "entitycollisionfix";
        table["extended"] = "extended";
        table["fabric api"] = "fabric-api";
        table["fabric api base"] = "fabric-api";
        table["fabric-api-base"] = "fabric-api";
        table["fabric-language-kotlin"] = "fabric-language-kotlin";
        table["fabric language kotlin"] = "fabric-language-kotlin";
        table["fabric proxy"] = "fabric-proxy";
        table["fastanim"] = "fastanim";
        table["fast anim"] = "fastanim";
        table["first person model"] = "first-person-model";
        table["firstpersonmodel"] = "first-person-model";
        table["fps reducer"] = "fps-reducer";
        table["fpsreducer"] = "fps-reducer";
        table["geckolib"] = "geckolib";
        table["gecko lib"] = "geckolib";
        table["good ending"] = "good-ending";
        table["goodending"] = "good-ending";
        table["guard villagers"] = "guard-villagers";
        table["guardvillagers"] = "guard-villagers";
        table["here is no you"] = "here-is-no-you";
        table["horsestatsvanilla"] = "horsestatsvanilla";
        table["horse stats vanilla"] = "horsestatsvanilla";
        table["Iris"] = "iris";
        table["item model fix"] = "item-model-fix";
        table["itemmodelfix"] = "item-model-fix";
        table["jamlib"] = "jamlib";
        table["kambrik"] = "kambrik";
        table["konkrete"] = "konkrete";
        table["lack of name binding"] = "lack-of-name-binding";
        table["lambdabettergrass"] = "lambdabettergrass";
        table["lambda better grass"] = "lambdabettergrass";
        table["lithium"] = "lithium";
        table["magnum torus"] = "magnum-torus";
        table["malilib"] = "malilib";
        table["memory usage screen"] = "memory-usage-screen";
        table["memoryusagescreen"] = "memory-usage-screen";
        table["midnightlib"] = "midnightlib";
        table["midnight lib"] = "midnightlib";
        table["minecraft under water"] = "minecraft-under-water";
        table["more armor"] = "more-armor";
        table["more armor trims"] = "more-armor-trims";
        table["more chests"] = "chested";
        table["more culling"] = "moreculling";
        table["morechipped"] = "more-chipped";
        table["more overlays"] = "more-overlays";
        table["more overlays updated"] = "more-overlays-updated";
        table["more slabs"] = "more-slabs";
        table["my nethers delite"] = "my-nethers-delight";
        table["naturalist"] = "naturalist";
        table["no weak attack"] = "no-weak-attack";
        table["not culling"] = "not-culling";
        table["nullscape"] = "nullscape";
        table["oceans delite"] = "oceans-delight";
        table["occurrence"] = "current";
        table["oceans delight"] = "oceans-delight";
        table["origins"] = "origins";
        table["patbox"] = "patbox";
        table["patchouli"] = "patchouli";
        table["pick up notifier"] = "pick-up-notifier";
        table["pickupnotifier"] = "pick-up-notifier";
        table["polymer"] = "polymer";
        table["pomegranate"] = "pomegranate";
        table["prickle"] = "prickle";
        table["prism"] = "prism";
        table["progressive boss health"] = "progressive-boss-health";
        table["puzzles lib"] = "puzzles-lib";
        table["puzzleslib"] = "puzzles-lib";
        table["rei"] = "roughly-enough-items";
        table["roughly enough items"] = "roughly-enough-items";
        table["reroll"] = "reroll";
        table["resources full"] = "resources-full";
        table["right click harvest"] = "right-click-harvest";
        table["rightclickharvest"] = "right-click-harvest";
        table["seamless loading screen"] = "seamless-loading-screen";
        table["seamless"] = "seamless-loading-screen";
        table["semitranslucence"] = "semitranslucence";
        table["serversidesmoothchunk"] = "smoothchunks";
        table["shimmer"] = "shimmer";
        table["simple hats"] = "simple-hats";
        table["simple voice chat"] = "simple-voice-chat";
        table["simplevoicechat"] = "simple-voice-chat";
        table["skylib"] = "skylib";
        table["slime to be"] = "slimetobe";
        table["smoothchunks"] = "smoothchunks";
        table["spark"] = "spark";
        table["spell engine"] = "spell-engine";
        table["status effect bars"] = "status-effect-bars";
        table["status effect timer"] = "status-effect-timer";
        table["stone zone"] = "stonezone";
        table["strange"] = "strange";
        table["supplementaries"] = "supplementaries";
        table["table"] = "table";
        table["taterzens"] = "taterzens";
        table["technology"] = "technology";
        table["tempad"] = "tempad";
        table["tetra"] = "tetra";
        table["the one probe"] = "the-one-probe";
        table["theoneprobe"] = "the-one-probe";
        table["tiered"] = "tiered";
        table["tier sort"] = "tier-sort";
        table["tips"] = "tips";
        table["tool kit c2me"] = "c2me";
        table["tool stats"] = "tool-stats";
        table["totem of veiling"] = "totem-of-veiling";
        table["trade cycling"] = "trade-cycling";
        table["travelers titles"] = "travelers-titles";
        table["trinkets"] = "trinkets";
        table["twigs"] = "twigs";
        table["uwu"] = "owo";
        table["vampirism"] = "vampirism";
        table["vein miner"] = "vein-miner";
        table["veinminer"] = "vein-miner";
        table["view collapse"] = "view-collapse";
        table["visual workbench"] = "visualworkbench";
        table["void frag"] = "voidfrag";
        table["void fog"] = "voidfog";
        table["war dance"] = "war-dance";
        table["waterdance"] = "war-dance";
        table["wthit"] = "wthit";
        table["wthit for fabric"] = "wthit";
        table["wthit forge"] = "wthit-forge";
        table["xaeroplus"] = "xaeroplus";
        table["xenon"] = "xenon";
        table["yacl"] = "yacl";
        table["yacl lib"] = "yacl";
        table["yetus"] = "yetus";
        table["youre in grave danger"] = "youre-in-grave-danger";
        table["yungs"] = "yungs-api";
        table["zume"] = "zume";
    }
    return table;
}

// Non-slug aliases: these map to known Modrinth project IDs
// Used for direct project endpoint lookups (faster than search)
inline QHash<QString, QString> directSlugMap() {
    static QHash<QString, QString> map;
    if (map.isEmpty()) {
        map["fabric api"] = "fabric-api";
        map["fabric-api"] = "fabric-api";
        map["cloth config api"] = "cloth-config";
        map["cloth-config"] = "cloth-config";
        map["architectury api"] = "architectury-api";
        map["architectury"] = "architectury-api";
        map["architectury-api"] = "architectury-api";
        map["mod menu"] = "modmenu";
        map["modmenu"] = "modmenu";
        map["sodium"] = "sodium";
        map["lithium"] = "lithium";
        map["phosphor"] = "phosphor";
        map["iris"] = "iris";
        map["just-enough-items"] = "jei";
        map["jei"] = "jei";
        map["roughly-enough-items"] = "roughly-enough-items";
        map["rei"] = "roughly-enough-items";
        map["emi"] = "emi";
    map["create"] = "create";
    map["create-fabric"] = "create-fabric";
    map["canvas"] = "canvas";
    map["appleskin"] = "appleskin";
    map["apple skin"] = "appleskin";
    map["jade"] = "jade";
    map["entity-model-features"] = "entity-model-features";
    map["emf"] = "entity-model-features";
    map["entity-texture-features"] = "entitytexturefeatures";
    map["etf"] = "entitytexturefeatures";
    map["entitytexturefeatures"] = "entitytexturefeatures";
    map["noisium"] = "noisium";
    map["xaeros-minimap"] = "xaeros-minimap";
    map["xaeros-world-map"] = "xaeros-world-map";
    map["yungs-api"] = "yungs-api";
    map["yungs-better-dungeons"] = "yungs-better-dungeons";
    map["yungs-better-mineshafts"] = "yungs-better-mineshafts";
    map["yungs-better-strongholds"] = "yungs-better-strongholds";
    map["reeses-sodium-options"] = "reeses-sodium-options";
    map["cull-leaves"] = "cull-leaves";
    map["continuity"] = "continuity";
    map["entityculling"] = "entityculling";
    map["fallingleaves"] = "fallingleaves";
    map["falling leaves"] = "fallingleaves";
    }
    return map;
}

inline QString stripBrackets(const QString& input) {
    static const QRegularExpression bracketRegex(R"([\(\[\{].*?[\)\]\}])");
    QString result = input;
    result.remove(bracketRegex);
    return result.simplified();
}

// Aggressive normalization: strips brackets, loader prefixes, version suffixes,
// required tags — everything that isn't part of the actual mod name
inline QString sanitizeModName(const QString& input) {
    QString result = stripBrackets(input);

    // Remove leading loader tags at start of string like "[Fabric]",
    // "Fabric", "Forge" etc that aren't part of the mod name
    result = result.remove(QRegularExpression(
        R"(^(fabric|forge|quilt|neoforge)\b\s*)",
        QRegularExpression::CaseInsensitiveOption
    ));

    // Remove [Required], (Fabric/Forge), [Fabric/Forge] brackets with slashes
    result = result.remove(QRegularExpression(
        R"(\[Required\]|\([Ff]abric/[Ff]orge\)|\[[Ff]abric/[Ff]orge\])"
    ));

    // Remove trailing version-like patterns:
    // " v0.11.1", "-0.5.3", "[1.20.1]", "(MC 1.20.1)", "+1.20.1" etc
    result = result.remove(QRegularExpression(
        R"(\s*[\(\[]?[vV]?\d+(\.\d+)+[^)]*[\)\]\}]?\s*$)"
    ));
    // Remove trailing version with MC marker
    result = result.remove(QRegularExpression(
        R"(\s*[-_\(\[\{]*(mc|forge|fabric|neoforge)[-_\s]*\d+(\.\d+)*.*$)",
        QRegularExpression::CaseInsensitiveOption
    ));

    // Replace dashes/underscores with spaces
    result = result.replace(QRegularExpression("[-_]"), " ");

    // Collapse repeated whitespace
    result = result.simplified().trimmed();

    return result;
}

inline QString normalizeForConflict(const QString& input) {
    QString r = input.toLower();
    r.remove(QRegularExpression(R"([^a-z0-9])"));
    return r;
}

// Get a "slug key" from the alias table if one exists
// Returns empty string if no alias found
inline QString getDirectSlug(const QString& normalized) {
    QString lowered = normalized.toLower().trimmed();
    // Collapse spaces for lookup
    QString collapsed = lowered;
    collapsed.replace(QRegularExpression(R"(\s+)"), " ");
    collapsed = collapsed.simplified();

    auto direct = directSlugMap();
    if (direct.contains(collapsed)) {
        return direct[collapsed];
    }

    // Try alias table as fallback
    auto aliases = aliasTable();
    if (aliases.contains(collapsed)) {
        return aliases[collapsed];
    }

    // Try normalizeForConflict match
    QString norm = normalizeForConflict(normalized);
    for (auto it = direct.constBegin(); it != direct.constEnd(); ++it) {
        QString keyNorm = normalizeForConflict(it.key());
        if (keyNorm == norm) {
            return it.value();
        }
    }
    for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
        QString keyNorm = normalizeForConflict(it.key());
        if (keyNorm == norm) {
            return it.value();
        }
    }

    return {};
}

// Check if a normalized name maps to a known direct slug
inline bool hasDirectSlug(const QString& normalized) {
    return !getDirectSlug(normalized).isEmpty();
}

// Extract acronym from parentheses, e.g. "Roughly Enough Items (REI)" -> "rei"
inline QString extractAcronym(const QString& input) {
    static const QRegularExpression acroRe(R"(\(([A-Za-z0-9]+)\))");
    auto match = acroRe.match(input);
    if (match.hasMatch()) {
        QString acro = match.captured(1).toLower().trimmed();
        if (acro.length() > 0 && acro.length() <= 8) {
            bool hasLetter = false;
            for (const QChar& c : acro) { if (c.isLetter()) { hasLetter = true; break; } }
            if (hasLetter) return acro;
        }
    }
    return {};
}

// Resolve a mod name through aliases (if applicable), returns normalized slug
inline QString resolveAlias(const QString& input) {
    QString lowered = input.toLower().trimmed();
    // Check direct alias
    QHash<QString, QString> table = aliasTable();
    if (table.contains(lowered)) {
        return table[lowered];
    }
    // Check normalized version
    QString norm = normalizeForConflict(input);
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        QString keyNorm = normalizeForConflict(it.key());
        if (keyNorm == norm || norm.contains(keyNorm) || keyNorm.contains(norm)) {
            return it.value();
        }
    }
    return {};
}

// Generate ALL possible normalized forms for comprehensive conflict matching
inline QStringList allNormalizedForms(const QString& input) {
    QStringList forms;
    QString base = normalizeForConflict(input);
    forms.append(base);

    // Try acronym extraction
    QString acro = extractAcronym(input);
    if (!acro.isEmpty()) {
        forms.append(acro);
        QString withoutBrackets = normalizeForConflict(stripBrackets(input)).simplified();
        withoutBrackets.remove(' ');
        if (!withoutBrackets.isEmpty() && !forms.contains(withoutBrackets)) {
            forms.append(withoutBrackets);
        }
    }

    // Try alias resolution
    QString alias = resolveAlias(input);
    if (!alias.isEmpty()) {
        forms.append(normalizeForConflict(alias));
    }

    // Try just the first word if the name is long
    if (base.length() > 8) {
        QStringList words = input.toLower().split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
        for (const auto& w : words) {
            QString nw = normalizeForConflict(w);
            if (nw.length() >= 2 && nw.length() <= 12 && !forms.contains(nw)) {
                forms.append(nw);
            }
        }
    }

    return forms;
}

inline QString splitCamelCase(const QString& input) {
    QRegularExpression re("(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])");
    QString result = input;
    return result.replace(re, " ");
}

// Levenshtein distance between two strings
inline int levenshteinDistance(const QString& s1, const QString& s2) {
    int m = s1.length(), n = s2.length();
    if (m == 0) return n;
    if (n == 0) return m;

    QVector<QVector<int>> dp(m + 1, QVector<int>(n + 1));
    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;

    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i-1][j] + 1, dp[i][j-1] + 1, dp[i-1][j-1] + cost});
        }
    }
    return dp[m][n];
}

// Similarity ratio from 0.0 to 1.0
inline double similarityRatio(const QString& a, const QString& b) {
    if (a.isEmpty() && b.isEmpty()) return 1.0;
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    int dist = levenshteinDistance(a.toLower(), b.toLower());
    int maxLen = std::max(a.length(), b.length());
    return 1.0 - (static_cast<double>(dist) / maxLen);
}

// Check if the primary word of the query exists in the result
inline bool primaryWordExists(const QString& query, const QString& result) {
    QStringList queryWords = query.toLower().split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
    QStringList resultWords = result.toLower().split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
    if (queryWords.isEmpty()) return true;
    for (const auto& qw : queryWords) {
        for (const auto& rw : resultWords) {
            if (rw.contains(qw) || qw.contains(rw)) return true;
        }
    }
    return false;
}

} // namespace StringCleaner