#include "test_framework.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/script/translate.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

using namespace aven;
using namespace aven::script;

namespace {

const TargetLanguage kAll[] = {TargetLanguage::Unity, TargetLanguage::Godot, TargetLanguage::Roblox, TargetLanguage::Unreal};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

AVEN_TEST(translate_every_template_script) {
    // Set AVEN_DUMP_TRANSLATIONS=folder to write the translations out for reading.
    const char* dump = std::getenv("AVEN_DUMP_TRANSLATIONS");
    int count = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::path(AVEN_SOURCE_DIR) / "templates")) {
        std::string ext = entry.path().extension().string();
        if (ext != ".es" && ext != ".blocks")
            continue;
        auto text = fs::readText(entry.path());
        CHECK(text.has_value());
        std::string source = *text;
        if (ext == ".blocks") {
            std::string error;
            source = blocks::compileFile(*text, &error);
            CHECK(error.empty());
        }
        bool is3D = contains(entry.path().string(), "3d");
        for (TargetLanguage lang : kAll) {
            TranslateOptions options;
            options.className = classNameFor(entry.path().string());
            options.is3D = is3D;
            Translation t = translate(source, lang, options);
            if (!t.ok)
                std::printf("  %s: %s (line %d)\n", entry.path().string().c_str(), t.error.c_str(), t.errorLine);
            CHECK(t.ok);
            CHECK(!t.code.empty());
            // No EasyScript-only syntax should leak through.
            CHECK(!contains(t.code, " ? ") || lang == TargetLanguage::Unity || lang == TargetLanguage::Unreal);
            CHECK(!contains(t.code, "self.") || lang == TargetLanguage::Godot);
            if (dump) {
                std::filesystem::path out = std::filesystem::path(dump) / languageEngine(lang) /
                                            (entry.path().parent_path().parent_path().filename().string() + "_" +
                                             entry.path().stem().string() + languageExtension(lang));
                std::string notes;
                for (auto& n : t.notes)
                    notes += "// NOTE: " + n + "\n";
                fs::writeText(out, t.code + "\n" + notes);
            }
            ++count;
        }
    }
    CHECK(count >= 40);
}

AVEN_TEST(translate_maps_events_and_api) {
    const char* source = R"(# A slime
speed = 1.5  # how fast it walks
_dir = 1

def on_update(dt):
    self.x += speed * _dir * dt
    if key_pressed("space") and self.on_ground:
        self.velocity_y = 8
    elif self.x > 3:
        _dir = -1

def on_trigger(other):
    if other.tag == "player":
        game.coins += 1
        self.destroy()
)";
    Translation u = translate(source, TargetLanguage::Unity, {"Slime", false});
    CHECK(u.ok);
    CHECK(contains(u.code, "public class Slime : MonoBehaviour"));
    CHECK(contains(u.code, "[Tooltip(\"how fast it walks\")]"));
    CHECK(contains(u.code, "public float speed = 1.5f;"));
    CHECK(contains(u.code, "void Update()"));
    CHECK(contains(u.code, "transform.position += new Vector3(speed * dir * Time.deltaTime, 0, 0);"));
    CHECK(contains(u.code, "Input.GetKeyDown(KeyCode.Space) && IsGrounded()"));
    CHECK(contains(u.code, "rb.velocity = new Vector2(rb.velocity.x, 8);"));
    CHECK(contains(u.code, "void OnTriggerEnter2D(Collider2D col)"));
    CHECK(contains(u.code, "other.CompareTag(\"player\")"));
    CHECK(contains(u.code, "GameState.coins += 1;"));
    CHECK(contains(u.code, "Destroy(gameObject);"));
    CHECK(contains(u.code, "// A slime"));

    Translation g = translate(source, TargetLanguage::Godot, {"Slime", false});
    CHECK(g.ok);
    CHECK(contains(g.code, "extends RigidBody2D"));
    CHECK(contains(g.code, "@export var speed: float = 1.5  ## how fast it walks"));
    CHECK(contains(g.code, "func _process(delta):"));
    CHECK(contains(g.code, "position.x += speed * _dir * delta"));
    CHECK(contains(g.code, "elif position.x > 3:"));
    CHECK(contains(g.code, "body_entered.connect(_on_body_entered)"));
    CHECK(contains(g.code, "queue_free()"));

    Translation r = translate(source, TargetLanguage::Roblox, {"Slime", false});
    CHECK(r.ok);
    CHECK(contains(r.code, "local part = script.Parent"));
    CHECK(contains(r.code, "RunService.Heartbeat:Connect(function(dt)"));
    CHECK(contains(r.code, "part.Position += Vector3.new(speed * dir * dt, 0, 0)"));
    CHECK(contains(r.code, "elseif part.Position.X > 3 then"));
    CHECK(contains(r.code, "part.Touched:Connect(function(other)"));
    CHECK(contains(r.code, "part:Destroy()"));
    CHECK(contains(r.code, "end)"));

    Translation e = translate(source, TargetLanguage::Unreal, {"Slime", false});
    CHECK(e.ok);
    CHECK(contains(e.code, "class ASlime : public AActor"));
    CHECK(contains(e.code, "UPROPERTY(EditAnywhere, meta = (ToolTip = \"how fast it walks\"))"));
    CHECK(contains(e.code, "virtual void Tick(float DeltaTime) override"));
    CHECK(contains(e.code, "AddActorWorldOffset(FVector(Speed * Dir * DeltaTime, 0, 0));"));
    CHECK(contains(e.code, "virtual void NotifyActorBeginOverlap(AActor* Other) override"));
    CHECK(contains(e.code, "Other->ActorHasTag(TEXT(\"player\"))"));
    CHECK(contains(e.code, "GetGameInstance<UMyGameInstance>()->Coins += 1;"));
}

AVEN_TEST(translate_reports_syntax_errors) {
    Translation t = translate("def on_update(dt)\n    pass\n", TargetLanguage::Unity);
    CHECK(!t.ok);
    CHECK(t.errorLine == 1);
    CHECK(!t.error.empty());
    CHECK_EQ(classNameFor("scripts/slime_enemy.es"), std::string("SlimeEnemy"));
}

#include "aven/audio/sfx.h"

AVEN_TEST(sfx_presets_make_sound) {
    for (const char* kind : {"coin", "laser", "explosion", "powerup", "hurt", "jump", "blip", "random"}) {
        for (uint32_t seed : {1u, 2u, 3u}) {
            aven::SfxParams p = aven::sfxPreset(kind, seed);
            std::vector<float> s = aven::sfxSynthesize(p);
            CHECK(s.size() > 100);
            CHECK(s.size() <= static_cast<size_t>(aven::kSfxSampleRate) * 6);
            float peak = 0;
            for (float v : s) {
                CHECK(v >= -1.0f && v <= 1.0f);
                peak = std::max(peak, std::abs(v));
            }
            CHECK(peak > 0.001f); // not silent
            // Settings survive a round trip through the .sfx file format.
            aven::SfxParams back = aven::SfxParams::fromJson(p.toJson());
            CHECK_EQ(back.wave, p.wave);
            CHECK_NEAR(back.baseFreq, p.baseFreq, 0.001f);
        }
    }
    auto path = std::filesystem::temp_directory_path() / "aven_sfx_test.wav";
    CHECK(aven::writeWav(path, aven::sfxSynthesize(aven::sfxPreset("coin", 7))));
    auto bytes = fs::readBinary(path);
    CHECK(bytes && bytes->size() > 44 && std::memcmp(bytes->data(), "RIFF", 4) == 0);
}
