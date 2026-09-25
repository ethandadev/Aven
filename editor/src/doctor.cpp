// The Error doctor: turns errors into plain English with a Fix button, and checks a game for
// common mistakes before they turn into confusing bugs (a script setting velocity on an object
// without physics, a find("Plyer") typo, a missing prefab...).

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/render/scene_renderer.h"
#include "block_editor.h"
#include "code_editor.h"
#include "script_facts.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <regex>

namespace aven::editor {

namespace {

int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j) {
            int cost = std::tolower(static_cast<unsigned char>(a[i - 1])) == std::tolower(static_cast<unsigned char>(b[j - 1])) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

std::string closest(const std::string& word, const std::vector<std::string>& options, int maxDistance = 4) {
    std::string best;
    int bestD = maxDistance + 1;
    for (auto& o : options) {
        int d = editDistance(word, o);
        if (d < bestD) {
            bestD = d;
            best = o;
        }
    }
    return best;
}

// Text between the first pair of single quotes after `from`. Apostrophes inside words
// ("don't", "Can't") are not quotes.
std::string quotedAfter(const std::string& text, size_t from = 0) {
    auto word = [&](size_t i) { return i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) != 0); };
    for (size_t a = text.find('\'', from); a != std::string::npos; a = text.find('\'', a + 1)) {
        if (a > 0 && word(a - 1) && word(a + 1))
            continue; // an apostrophe
        for (size_t b = text.find('\'', a + 1); b != std::string::npos; b = text.find('\'', b + 1))
            if (!(word(b - 1) && word(b + 1)))
                return text.substr(a + 1, b - a - 1);
        return "";
    }
    return "";
}

std::string lineOf(const std::string& text, int line) {
    size_t start = 0;
    for (int i = 1; i < line && start != std::string::npos; ++i) {
        start = text.find('\n', start);
        if (start != std::string::npos)
            ++start;
    }
    if (start == std::string::npos)
        return "";
    size_t end = text.find('\n', start);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string indentOf(const std::string& line) { return line.substr(0, line.find_first_not_of(" \t")); }

} // namespace

// ---------------------------------------------------------------- fixing text

bool Editor::rewriteScriptLine(const std::string& file, int line, const std::function<std::string(const std::string&)>& change) {
    auto text = fs::readText(projectDir_ / file);
    if (!text || line <= 0)
        return false;
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        size_t nl = text->find('\n', start);
        lines.push_back(text->substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    if (line > static_cast<int>(lines.size()))
        return false;
    lines[static_cast<size_t>(line - 1)] = change(lines[static_cast<size_t>(line - 1)]);
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
        out += lines[i] + (i + 1 < lines.size() ? "\n" : "");
    fs::writeText(projectDir_ / file, out);
    // Keep an open editor tab in sync.
    for (auto& t : tabs_)
        if (t->path == file && t->code) {
            t->code->setText(out);
            t->code->gotoLine(line);
            t->modified = false;
            checkScript(*t);
        }
    factsCache_.erase(file);
    return true;
}

bool Editor::replaceWordInLine(const std::string& file, int line, const std::string& from, const std::string& to) {
    return rewriteScriptLine(file, line, [&](const std::string& l) {
        std::string out = l;
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            bool startOk = pos == 0 || !(std::isalnum(static_cast<unsigned char>(out[pos - 1])) || out[pos - 1] == '_');
            size_t end = pos + from.size();
            bool endOk = end >= out.size() || !(std::isalnum(static_cast<unsigned char>(out[end])) || out[end] == '_');
            if (startOk && endOk) {
                out.replace(pos, from.size(), to);
                pos += to.size();
            } else {
                pos = end;
            }
        }
        return out;
    });
}

// ---------------------------------------------------------------- diagnosing one error

namespace {

const Json* matchDoctorRule(const std::string& message) {
    std::string m = message;
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto& rule : editorData("doctor_rules.json")["rules"].elements()) {
        std::string match = rule["match"].asString("");
        std::transform(match.begin(), match.end(), match.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!match.empty() && m.find(match) != std::string::npos)
            return &rule;
    }
    return nullptr;
}

} // namespace

std::vector<Editor::Diagnosis> Editor::diagnose(const std::string& message, const std::string& file, int line) {
    std::vector<Diagnosis> out;
    Diagnosis d;
    d.file = file;
    d.line = line;
    d.original = message;
    std::string source = file.empty() ? "" : fs::readText(projectDir_ / file).value_or("");
    std::string code = lineOf(source, line);
    bool textFile = fs::extension(file) == ".es";
    std::string where = file.empty() ? "" : " (" + file + " line " + std::to_string(line) + ")";
    auto openFix = [&] {
        if (!file.empty()) {
            std::string f = file;
            int l = line;
            d.fixes.push_back({"Open " + stdfs::path(file).filename().string() + " at line " + std::to_string(line),
                               [f, l](Editor& ed) { ed.openScript(f, l); }});
        }
    };
    auto suggestion = [&]() -> std::string {
        size_t p = message.find("Did you mean");
        return p == std::string::npos ? "" : quotedAfter(message, p);
    };

    if (message.find("I don't know what '") != std::string::npos) {
        std::string word = quotedAfter(message);
        std::string sug = suggestion();
        d.title = "Unknown name: " + word;
        d.explanation = "The script uses '" + word + "', but nothing has that name. It's usually a typo, or a variable that "
                        "was never created" + where + ".";
        if (!sug.empty() && textFile) {
            std::string f = file;
            int l = line;
            d.explanation += " Did you mean '" + sug + "'?";
            d.fixes.push_back({"Change '" + word + "' to '" + sug + "'", [f, l, word, sug](Editor& ed) {
                                   ed.replaceWordInLine(f, l, word, sug);
                               }});
        } else if (textFile) {
            std::string f = file;
            d.fixes.push_back({"Create the variable: " + word + " = 0 (at the top of the script)", [f, word](Editor& ed) {
                                   ed.rewriteScriptLine(f, 1, [&](const std::string& l) { return word + " = 0\n" + l; });
                               }});
        }
        openFix();
    } else if (message.find("Use '==' to compare") != std::string::npos) {
        d.title = "One '=' where two were needed";
        d.explanation = "A single '=' stores a value. To check whether two things are equal, use '=='" + where + ".";
        if (textFile && code.find('=') != std::string::npos) {
            std::string f = file;
            int l = line;
            d.fixes.push_back({"Change '=' to '==' on line " + std::to_string(line), [f, l](Editor& ed) {
                                   ed.rewriteScriptLine(f, l, [](const std::string& s) {
                                       for (size_t i = 0; i < s.size(); ++i)
                                           if (s[i] == '=' && (i == 0 || (s[i - 1] != '=' && s[i - 1] != '!' && s[i - 1] != '<' && s[i - 1] != '>')) &&
                                               (i + 1 >= s.size() || s[i + 1] != '='))
                                               return s.substr(0, i) + "==" + s.substr(i + 1);
                                       return s;
                                   });
                               }});
        }
        openFix();
    } else if (message.find("instead of { }") != std::string::npos || message.find("':' at the end") != std::string::npos ||
               (message.find("Expected ':'") != std::string::npos)) {
        d.title = "A block needs a colon";
        d.explanation = "Lines that start a block (if, else, for, while, def) end with ':' and the lines inside are indented. "
                        "EasyScript doesn't use { } like C or JavaScript" + where + ".";
        std::string trimmed = code.substr(code.find_first_not_of(" \t") == std::string::npos ? 0 : code.find_first_not_of(" \t"));
        bool starts = trimmed.rfind("if", 0) == 0 || trimmed.rfind("elif", 0) == 0 || trimmed.rfind("else", 0) == 0 ||
                      trimmed.rfind("for", 0) == 0 || trimmed.rfind("while", 0) == 0 || trimmed.rfind("def", 0) == 0;
        if (textFile && starts) {
            std::string f = file;
            int l = line;
            d.fixes.push_back({"Write it the EasyScript way (':' and no braces)", [f, l](Editor& ed) {
                                   ed.rewriteScriptLine(f, l, [](const std::string& s) {
                                       std::string t = s;
                                       t.erase(std::remove(t.begin(), t.end(), '{'), t.end());
                                       while (!t.empty() && (t.back() == ' ' || t.back() == '\t'))
                                           t.pop_back();
                                       if (!t.empty() && t.back() != ':')
                                           t += ":";
                                       return t;
                                   });
                               }});
        }
        openFix();
    } else if (message.find("indented more than it should") != std::string::npos ||
               message.find("indentation doesn't line up") != std::string::npos ||
               message.find("This line is indented, but nothing above it") != std::string::npos) {
        d.title = "The spaces at the start of a line are off";
        d.explanation = "In EasyScript, the spaces at the start of a line show which block it belongs to. This line's spaces don't "
                        "match the lines around it" + where + ".";
        if (textFile && line > 1) {
            std::string prev;
            for (int l = line - 1; l >= 1; --l) {
                prev = lineOf(source, l);
                if (prev.find_first_not_of(" \t") != std::string::npos)
                    break;
            }
            std::string target = indentOf(prev);
            std::string trimmedPrev = prev.substr(target.size());
            if (!trimmedPrev.empty() && trimmedPrev.back() == ':')
                target += "    ";
            std::string f = file;
            int l = line;
            d.fixes.push_back({"Line it up with the line above", [f, l, target](Editor& ed) {
                                   ed.rewriteScriptLine(f, l, [&](const std::string& s) {
                                       size_t first = s.find_first_not_of(" \t");
                                       return target + (first == std::string::npos ? "" : s.substr(first));
                                   });
                               }});
        }
        openFix();
    } else if (message.find("doesn't show text") != std::string::npos || message.find("has nothing to color") != std::string::npos) {
        std::string name = quotedAfter(message);
        bool text = message.find("text") != std::string::npos && message.find("color") == std::string::npos;
        d.title = text ? "Text on an object that can't show text" : "Color on an object with nothing to color";
        d.explanation = "The script changes " + std::string(text ? "the text" : "the color") + " of '" + name + "', but '" + name +
                        "' has no " + (text ? "TextRenderer or UIText" : "SpriteRenderer") + " to show it" + where + ".";
        d.entity = name;
        d.fixes.push_back({std::string("Add a ") + (text ? "TextRenderer" : "SpriteRenderer") + " to " + name, [name, text](Editor& ed) {
                               if (Entity e = ed.editScene().findByName(name)) {
                                   ed.recordUndo("Doctor fix");
                                   ed.addComponentByName(e, text ? "TextRenderer" : "SpriteRenderer");
                               }
                           }});
        openFix();
    } else if (message.find("has no RigidBody2D, so changing its velocity does nothing") != std::string::npos) {
        std::string name = quotedAfter(message);
        d.title = "Velocity without physics";
        d.explanation = "'" + name + "' has its velocity (speed) set by a script, but velocity only works on objects with physics. "
                        "Nothing happens until it gets a RigidBody2D.";
        d.entity = name;
        d.warning = true;
        d.fixes.push_back({"Add a RigidBody2D and a collider to " + name, [name](Editor& ed) {
                               if (Entity e = ed.editScene().findByName(name)) {
                                   ed.recordUndo("Doctor fix");
                                   ed.addPhysics2D(e, false);
                               }
                           }});
    } else if (message.find("Can't find the prefab '") != std::string::npos || message.find("Can't find the scene '") != std::string::npos) {
        bool prefab = message.find("prefab") != std::string::npos;
        std::string missing = quotedAfter(message);
        auto options = projectFiles({prefab ? ".prefab" : ".scene"});
        std::string near = closest(missing, options, 12);
        d.title = std::string("Missing ") + (prefab ? "prefab" : "scene") + ": " + missing;
        d.explanation = "Something asks for '" + missing + "', but there's no file with that name in the project.";
        if (!near.empty() && d.file.empty()) {
            // Find where the name is used.
            for (auto& f : projectFiles({".es"})) {
                ScriptFacts& facts = factsFor(f);
                auto it = facts.fileLines.find(missing);
                if (it != facts.fileLines.end()) {
                    d.file = f;
                    d.line = it->second;
                    break;
                }
            }
        }
        if (!near.empty() && fs::extension(d.file) == ".es") {
            std::string f = d.file;
            int l = d.line;
            d.explanation += " The closest one is '" + near + "'.";
            d.fixes.push_back({"Use " + near + " instead", [f, l, missing, near](Editor& ed) {
                                   ed.rewriteScriptLine(f, l, [&](const std::string& s) {
                                       std::string out = s;
                                       size_t p = out.find(missing);
                                       if (p != std::string::npos)
                                           out.replace(p, missing.size(), near);
                                       return out;
                                   });
                               }});
        }
        if (prefab)
            d.explanation += " Make a prefab by right-clicking an object in the Hierarchy and choosing 'Save as Prefab'.";
    } else if (message.find("hasn't been given a value yet") != std::string::npos) {
        size_t p = message.find("game.");
        std::string var;
        for (size_t i = p + 5; p != std::string::npos && i < message.size() && (std::isalnum(static_cast<unsigned char>(message[i])) || message[i] == '_'); ++i)
            var += message[i];
        d.title = "game." + var + " is used before it's set";
        d.explanation = "Game variables (game.something) must get a value before anything reads them, usually in on_start() of "
                        "one script" + where + ".";
        if (textFile && !var.empty()) {
            std::string f = file;
            int l = line;
            d.fixes.push_back({"Read it with get_game(\"" + var + "\", 0), which gives 0 until it's set", [f, l, var](Editor& ed) {
                                   ed.rewriteScriptLine(f, l, [&](const std::string& s) {
                                       std::string out = s;
                                       std::string from = "game." + var;
                                       std::string to = "get_game(\"" + var + "\", 0)";
                                       // Only replace reads: keep "game.x = ..." assignments.
                                       size_t eq = out.find('=');
                                       size_t first = out.find(from);
                                       bool assignment = eq != std::string::npos && first != std::string::npos && first < eq &&
                                                         (eq + 1 >= out.size() || out[eq + 1] != '=') &&
                                                         out.find_first_not_of(" \t") == first;
                                       if (assignment && out.find("+=") != std::string::npos) {
                                           // game.x += 1  ->  game.x = get_game("x", 0) + 1
                                           size_t op = out.find("+=");
                                           return out.substr(0, op) + "= " + to + " +" + out.substr(op + 2);
                                       }
                                       size_t pos = assignment ? out.find(from, eq) : 0;
                                       while ((pos = out.find(from, pos)) != std::string::npos) {
                                           out.replace(pos, from.size(), to);
                                           pos += to.size();
                                       }
                                       return out;
                                   });
                               }});
        }
        openFix();
    } else if (message.find("is used before it has been given a value") != std::string::npos) {
        std::string var = quotedAfter(message);
        d.title = var + " is used before it has a value";
        d.explanation = "Give '" + var + "' a starting value at the top of the script, outside any function" + where + ".";
        if (textFile) {
            std::string f = file;
            d.fixes.push_back({"Add " + var + " = 0 at the top", [f, var](Editor& ed) {
                                   ed.rewriteScriptLine(f, 1, [&](const std::string& l) { return var + " = 0\n" + l; });
                               }});
        }
        openFix();
    } else if (message.find("ran for too long") != std::string::npos) {
        d.title = "A loop that never stops";
        d.explanation = "The script kept running without a break, which would freeze the game, so Aven paused it. A 'while' loop "
                        "probably never becomes false. Inside long loops, add wait() so the game can draw a frame" + where + ".";
        openFix();
    } else if (message.find("None (nothing)") != std::string::npos || message.find("isn't available. Check that it still exists") != std::string::npos) {
        d.title = "Something is missing (None)";
        d.explanation = "The script used an object or value that doesn't exist. Often find(\"Name\") didn't find anything "
                        "because of a typo in the name, or the object was destroyed" + where + ".";
        openFix();
    } else if (message.find("I don't know the key") != std::string::npos) {
        std::string sug = suggestion();
        size_t a = message.find('"');
        size_t b = a == std::string::npos ? a : message.find('"', a + 1);
        std::string key = a == std::string::npos || b == std::string::npos ? "" : message.substr(a + 1, b - a - 1);
        d.title = "Unknown key: " + key;
        d.explanation = "Keys are written in lowercase words, like \"space\", \"left\", \"a\" or \"enter\"" + where + ".";
        if (!sug.empty() && textFile && !key.empty()) {
            std::string f = file;
            int l = line;
            d.fixes.push_back({"Use \"" + sug + "\"", [f, l, key, sug](Editor& ed) {
                                   ed.rewriteScriptLine(f, l, [&](const std::string& s) {
                                       std::string out = s;
                                       size_t p = out.find("\"" + key + "\"");
                                       if (p != std::string::npos)
                                           out.replace(p, key.size() + 2, "\"" + sug + "\"");
                                       return out;
                                   });
                               }});
        }
        openFix();
    } else if (message.find("divide by zero") != std::string::npos) {
        d.title = "Dividing by zero";
        d.explanation = "Something was divided by 0, which has no answer. Check the value you divide by first, like: if count > 0:" + where + ".";
        openFix();
    } else if (!suggestion().empty() && textFile) {
        // Any other error with a suggestion: offer the swap.
        std::string word = quotedAfter(message), sug = suggestion();
        d.title = "Did you mean '" + sug + "'?";
        d.explanation = message;
        if (!word.empty() && word != sug) {
            std::string f = file;
            int l = line;
            d.fixes.push_back({"Change '" + word + "' to '" + sug + "'", [f, l, word, sug](Editor& ed) {
                                   ed.replaceWordInLine(f, l, word, sug);
                               }});
        }
        openFix();
    } else if (const Json* rule = matchDoctorRule(message)) {
        // Explanations from editor/data/doctor_rules.json (contributors add these).
        auto fillIn = [&](std::string text) {
            for (auto [key, value] : {std::pair<std::string, std::string>{"{file}", file.empty() ? "the script" : file},
                                      {"{line}", std::to_string(line)}})
                for (size_t p; (p = text.find(key)) != std::string::npos;)
                    text.replace(p, key.size(), value);
            return text;
        };
        d.title = fillIn((*rule)["title"].asString("Problem"));
        d.explanation = fillIn((*rule)["explanation"].asString(message));
        openFix();
    } else {
        d.title = file.empty() ? "Problem" : "Problem in " + stdfs::path(file).filename().string();
        d.explanation = message;
        openFix();
    }
    out.push_back(std::move(d));
    return out;
}

void Editor::addPhysics2D(Entity e, bool trigger) {
    auto& reg = editScene().registry();
    if (!reg.has<RigidBody2D>(e)) {
        auto& rb = reg.emplace<RigidBody2D>(e);
        rb.fixedRotation = true;
    }
    if (!reg.has<BoxCollider2D>(e) && !reg.has<CircleCollider2D>(e)) {
        auto* sr = reg.tryGet<SpriteRenderer>(e);
        auto& b = reg.emplace<BoxCollider2D>(e);
        b.size = sr ? sr->size : Vec2{1, 1};
        b.isTrigger = trigger;
    }
}

// ---------------------------------------------------------------- checkup

std::vector<Editor::Diagnosis> Editor::checkup() {
    std::vector<Diagnosis> out;
    Scene& s = editScene();
    auto& reg = s.registry();
    std::vector<std::string> names, tags;
    s.walk([&](Entity e, int) {
        names.push_back(s.info(e).name);
        if (!s.info(e).tag.empty())
            tags.push_back(s.info(e).tag);
        return true;
    });
    auto hasName = [&](const std::string& n) { return std::find(names.begin(), names.end(), n) != names.end(); };
    auto hasTag = [&](const std::string& t) {
        return std::find(tags.begin(), tags.end(), t) != tags.end() || hasName(t);
    };

    if (!SceneRenderer::findCamera(s)) {
        Diagnosis d;
        d.title = "No camera";
        d.explanation = "Without a camera, the game shows nothing when you press Play.";
        d.fixes.push_back({"Add a camera", [](Editor& ed) { ed.createEntity("Camera"); }});
        out.push_back(std::move(d));
    }

    std::set<std::string> checkedScripts;
    s.walk([&](Entity e, int) {
        std::string name = s.info(e).name;
        UUID id = s.info(e).uuid;
        // Missing files.
        auto missingFile = [&](const std::string& path, const char* what, const std::string& component, const std::string& field,
                               const std::vector<std::string>& exts) {
            std::error_code ec;
            if (path.empty() || stdfs::exists(projectDir_ / path, ec))
                return;
            Diagnosis d;
            d.title = std::string("Missing ") + what + ": " + path;
            d.explanation = name + " uses '" + path + "', but that file isn't in the project (maybe it was renamed or deleted).";
            d.entity = name;
            std::string near = closest(path, projectFiles(exts), 20);
            if (!near.empty())
                d.fixes.push_back({"Use " + near, [id, component, field, near](Editor& ed) {
                                       if (Entity x = ed.editScene().findByUUID(id)) {
                                           ed.recordUndo("Doctor fix");
                                           ed.setFieldValue(x, component, field, near);
                                       }
                                   }});
            d.fixes.push_back({"Remove the link", [id, component, field](Editor& ed) {
                                   if (Entity x = ed.editScene().findByUUID(id)) {
                                       ed.recordUndo("Doctor fix");
                                       ed.setFieldValue(x, component, field, "");
                                   }
                               }});
            out.push_back(std::move(d));
        };
        if (auto* sr = reg.tryGet<SpriteRenderer>(e))
            missingFile(sr->texture, "image", "SpriteRenderer", "texture", {".png", ".jpg", ".jpeg", ".bmp", ".tga"});
        if (auto* as = reg.tryGet<AudioSource>(e))
            missingFile(as->clip, "sound", "AudioSource", "clip", {".wav", ".mp3", ".ogg", ".flac"});
        if (auto* mr = reg.tryGet<MeshRenderer>(e); mr && mr->mesh == MeshShape::Model)
            missingFile(mr->model, "model", "MeshRenderer", "model", {".gltf", ".glb"});
        if (auto* sc = reg.tryGet<Script>(e))
            missingFile(sc->path, "script", "Script", "path", {".es", ".blocks"});

        // Scripts: syntax errors and physics/collision needs.
        auto* sc = reg.tryGet<Script>(e);
        std::error_code ec;
        if (sc && !sc->path.empty() && stdfs::exists(projectDir_ / sc->path, ec)) {
            ScriptFacts& f = factsFor(sc->path);
            if (!f.error.empty() && checkedScripts.insert(sc->path).second) {
                auto more = diagnose(f.error, sc->path, f.errorLine);
                out.insert(out.end(), more.begin(), more.end());
            }
            bool body = reg.has<RigidBody2D>(e) || reg.has<RigidBody>(e) || reg.has<CharacterController>(e);
            bool collider = reg.has<BoxCollider2D>(e) || reg.has<CircleCollider2D>(e) || reg.has<BoxCollider>(e) ||
                            reg.has<SphereCollider>(e) || reg.has<CharacterController>(e);
            if (f.usesPhysics && !body && !reg.has<MeshRenderer>(e)) {
                Diagnosis d;
                d.warning = true;
                d.title = name + " uses physics but has none";
                d.explanation = "Its script (" + sc->path + ") uses velocity or on_ground, which only work with a RigidBody2D.";
                d.entity = name;
                d.fixes.push_back({"Add a RigidBody2D and a collider", [id](Editor& ed) {
                                       if (Entity x = ed.editScene().findByUUID(id)) {
                                           ed.recordUndo("Doctor fix");
                                           ed.addPhysics2D(x, false);
                                       }
                                   }});
                out.push_back(std::move(d));
            }
            bool wantsTouch = f.functions.count("on_trigger") || f.functions.count("on_collide");
            if (wantsTouch && !collider) {
                Diagnosis d;
                d.warning = true;
                d.title = name + " can't notice touches";
                d.explanation = "Its script reacts to touches (on_trigger or on_collide), but it has no collider, so nothing can "
                                "touch it.";
                d.entity = name;
                bool trig = f.functions.count("on_trigger") > 0;
                d.fixes.push_back({trig ? "Add a trigger collider" : "Add a collider", [id, trig](Editor& ed) {
                                       if (Entity x = ed.editScene().findByUUID(id)) {
                                           ed.recordUndo("Doctor fix");
                                           auto& r = ed.editScene().registry();
                                           auto* sr = r.tryGet<SpriteRenderer>(x);
                                           if (r.has<MeshRenderer>(x))
                                               r.emplace<BoxCollider>(x).isTrigger = trig;
                                           else {
                                               auto& b = r.emplace<BoxCollider2D>(x);
                                               b.size = sr ? sr->size : Vec2{1, 1};
                                               b.isTrigger = trig;
                                           }
                                       }
                                   }});
                out.push_back(std::move(d));
            }
            // find("Name") typos.
            if (checkedScripts.insert(sc->path + "#names").second) {
                for (auto& n : f.findsNames)
                    if (!hasName(n)) {
                        Diagnosis d;
                        d.warning = true;
                        d.title = "find(\"" + n + "\") won't find anything";
                        d.explanation = sc->path + " looks for an object called '" + n + "', but there's none in this scene.";
                        std::string near = closest(n, names);
                        if (!near.empty())
                            d.explanation += " There's one called '" + near + "'.";
                        out.push_back(std::move(d));
                    }
                for (auto& t : f.tags)
                    if (!hasTag(t) && t != "anything") {
                        Diagnosis d;
                        d.warning = true;
                        d.title = "Nothing is tagged '" + t + "'";
                        d.explanation = sc->path + " checks for things tagged '" + t + "', but no object in this scene has that tag.";
                        // Objects named like the tag probably should have it.
                        std::vector<UUID> same;
                        s.walk([&](Entity o, int) {
                            std::string on = s.info(o).name;
                            std::transform(on.begin(), on.end(), on.begin(), ::tolower);
                            if (on == t && s.info(o).tag.empty())
                                same.push_back(s.info(o).uuid);
                            return true;
                        });
                        if (!same.empty())
                            d.fixes.push_back({"Tag the " + std::to_string(same.size()) + " object(s) named '" + t + "'", [same, t](Editor& ed) {
                                                   ed.recordUndo("Doctor fix");
                                                   for (UUID u : same)
                                                       if (Entity x = ed.editScene().findByUUID(u))
                                                           ed.editScene().info(x).tag = t;
                                               }});
                        out.push_back(std::move(d));
                    }
            }
        }
        // Behaviors that point at nothing.
        auto needsTag = [&](const std::string& behavior, const std::string& tag) {
            if (tag.empty() || hasTag(tag) || tag == "anything")
                return;
            Diagnosis d;
            d.warning = true;
            d.title = name + "'s " + behavior + " looks for '" + tag + "'";
            d.explanation = "No object in this scene is tagged '" + tag + "'. Tag your player (or the target) in the Inspector.";
            d.entity = name;
            out.push_back(std::move(d));
        };
        if (auto* c = reg.tryGet<Chase>(e))
            needsTag("Chase", c->targetTag);
        if (auto* h = reg.tryGet<Hazard>(e))
            needsTag("Hazard", h->victimTag);
        if (auto* c = reg.tryGet<Collectible>(e))
            needsTag("Collectible", c->collectorTag);
        auto emptyAsset = [&](const std::string& behavior, const std::string& value, const std::string& field, const char* what) {
            if (!value.empty())
                return;
            Diagnosis d;
            d.warning = true;
            d.title = name + "'s " + behavior + " has no " + what;
            d.explanation = "Pick a " + std::string(what) + " in the Inspector, or it won't do anything.";
            d.entity = name;
            if (behavior == "Shooter")
                d.fixes.push_back({"Use a simple bullet", [id](Editor& ed) {
                                       if (Entity x = ed.editScene().findByUUID(id)) {
                                           ed.recordUndo("Doctor fix");
                                           ed.ensureBulletPrefab();
                                           ed.setFieldValue(x, "Shooter", "prefab", "prefabs/bullet.prefab");
                                       }
                                   }});
            (void)field;
            out.push_back(std::move(d));
        };
        if (auto* sh = reg.tryGet<Shooter>(e))
            emptyAsset("Shooter", sh->prefab, "prefab", "prefab to fire");
        if (auto* sp = reg.tryGet<Spawner>(e))
            emptyAsset("Spawner", sp->prefab, "prefab", "prefab to create");
        if (auto* l = reg.tryGet<SceneLink>(e))
            emptyAsset("SceneLink", l->scene, "scene", "scene to go to");
        if (auto* cf = reg.tryGet<CameraFollow>(e); cf && cf->target && !s.findByUUID(cf->target)) {
            Diagnosis d;
            d.warning = true;
            d.title = "The camera follows an object that's gone";
            d.explanation = name + "'s CameraFollow points at an object that no longer exists. Pick a new Target.";
            d.entity = name;
            out.push_back(std::move(d));
        }
        return true;
    });
    return out;
}

// ---------------------------------------------------------------- UI

void Editor::openDoctorFor(const std::string& message, const std::string& file, int line) {
    doctorItems_ = diagnose(message, file, line);
    doctorTitle_ = "About this error";
    showDoctor_ = true;
    focusDoctor_ = true;
}

void Editor::runCheckup() {
    doctorItems_ = checkup();
    // Runtime errors from the last play session come first.
    std::vector<Diagnosis> fromConsole;
    std::set<std::string> seen;
    for (auto& l : console_)
        if ((l.level == LogLevel::Error || l.level == LogLevel::Warning) && seen.insert(l.text).second) {
            auto more = diagnose(l.text, l.file, l.line);
            for (auto& d : more)
                d.warning = l.level == LogLevel::Warning;
            fromConsole.insert(fromConsole.end(), more.begin(), more.end());
        }
    doctorItems_.insert(doctorItems_.begin(), fromConsole.begin(), fromConsole.end());
    doctorTitle_ = doctorItems_.empty() ? "" : "Checkup";
    doctorChecked_ = true;
}

// Shown over the game view when it paused itself because of an error.
void Editor::drawErrorBar(ImVec2 pos, ImVec2 size) {
    if (!playing_ || !paused_ || !pausedOnError_)
        return;
    float h = ImGui::GetFrameHeight() + 12;
    ImGui::SetCursorScreenPos({pos.x + 10, pos.y + size.y - h - 34});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(170, 45, 50, 240));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 245, 245, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6);
    ImGui::BeginChild("##errorbar", {std::min(size.x - 20, 780.0f), h}, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("The game paused because of an error.");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 20, 22, 255));
    if (unlocked(Feature::Doctor) && ImGui::SmallButton("What went wrong?")) {
        showDoctor_ = focusDoctor_ = true;
        if (doctorItems_.empty())
            runCheckup();
    }
    if (unlocked(Feature::BugReplay) && recorder_.recording()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Replay the last 20 s"))
            openBugReplay();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Keep playing")) {
        paused_ = false;
        pausedOnError_ = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Stop"))
        stop();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void Editor::drawDoctor() {
    ui::placeWindow({520, 560}, {0.62f, 0.42f});
    if (focusDoctor_) {
        ImGui::SetNextWindowFocus();
        focusDoctor_ = false;
    }
    std::string title = doctorItems_.empty() ? "Error Doctor###Doctor"
                                             : "Error Doctor (" + std::to_string(doctorItems_.size()) + ")###Doctor";
    if (!ImGui::Begin(title.c_str(), &showDoctor_)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Check my game"))
        runCheckup();
    ImGui::SameLine();
    ImGui::TextDisabled("Looks for errors and common mistakes.");
    ImGui::Separator();
    if (doctorItems_.empty()) {
        ImGui::Spacing();
        if (doctorChecked_) {
            ImGui::PushFont(fonts.bold);
            ImGui::TextColored({0.45f, 0.9f, 0.6f, 1}, "All good! No problems found.");
            ImGui::PopFont();
        } else {
            ImGui::TextWrapped("Click 'Check my game', or click an error in the Console, and the doctor will explain it in plain "
                               "words with a button to fix it.");
        }
        ImGui::End();
        return;
    }
    ImGui::BeginChild("##diagnoses");
    int fixedIndex = -1;
    for (size_t i = 0; i < doctorItems_.size(); ++i) {
        Diagnosis& d = doctorItems_[i];
        ImGui::PushID(static_cast<int>(i));
        ImVec4 accent = d.warning ? ImVec4(0.98f, 0.75f, 0.2f, 1) : ImVec4(0.95f, 0.35f, 0.35f, 1);
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::Indent(10);
        ImGui::PushFont(fonts.bold);
        ImGui::TextColored(accent, "%s", d.title.c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(d.explanation.c_str());
        ImGui::PopTextWrapPos();
        if (!d.original.empty() && d.original != d.explanation) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(("Original message: " + d.original).c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (!d.entity.empty()) {
            if (ImGui::SmallButton(("Select " + d.entity).c_str()))
                select(editScene().findByName(d.entity));
            ImGui::SameLine();
        }
        for (size_t f = 0; f < d.fixes.size(); ++f) {
            ImGui::PushID(static_cast<int>(f));
            bool isOpen = d.fixes[f].label.rfind("Open ", 0) == 0;
            if (!isOpen) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
            }
            if (ImGui::SmallButton((isOpen ? d.fixes[f].label : "Fix: " + d.fixes[f].label).c_str())) {
                if (playing_ && !isOpen)
                    stop();
                d.fixes[f].apply(*this);
                if (!isOpen) {
                    fixedIndex = static_cast<int>(i);
                    milestone("doctor_fixes");
                    notify("Fixed! Press Play to try again.");
                }
            }
            if (!isOpen)
                ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::PopID();
        }
        ImGui::NewLine();
        ImGui::Unindent(10);
        ImVec2 end = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(start, {start.x + 4, end.y - 4}, ImGui::ColorConvertFloat4ToU32(accent), 2);
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (fixedIndex >= 0)
        doctorItems_.erase(doctorItems_.begin() + fixedIndex);
    ImGui::End();
}

} // namespace aven::editor
