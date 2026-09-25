#include "test_framework.h"

#include "aven/core/json.h"
#include "aven/core/uuid.h"
#include "aven/math/math.h"

using namespace aven;

AVEN_TEST(math_vector_ops) {
    Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK(a + b == Vec3(5, 7, 9));
    CHECK_NEAR(dot(a, b), 32, 1e-6);
    CHECK(cross(Vec3(1, 0, 0), Vec3(0, 1, 0)) == Vec3(0, 0, 1));
    CHECK_NEAR(length(normalize(b)), 1, 1e-6);
}

AVEN_TEST(math_matrix_inverse) {
    Mat4 m = Mat4::trs({3, -2, 5}, Quat::fromEuler({30, 45, 60}), {2, 3, 0.5f});
    Mat4 id = m * inverse(m);
    for (int i = 0; i < 16; ++i)
        CHECK_NEAR(id.m[i], Mat4::identity().m[i], 1e-4);
}

AVEN_TEST(math_euler_roundtrip) {
    for (Vec3 e : {Vec3(10, 20, 30), Vec3(-45, 170, 5), Vec3(0, 0, 90), Vec3(80, -30, -120)}) {
        Vec3 back = Quat::fromEuler(e).toEuler();
        Quat q1 = Quat::fromEuler(e), q2 = Quat::fromEuler(back);
        CHECK_NEAR(std::abs(dot(q1, q2)), 1, 1e-4);
        CHECK_NEAR(back.x, e.x, 1e-2);
    }
}

AVEN_TEST(math_euler_order_matches_unity) {
    // Z is applied first, then X, then Y.
    Quat q = Quat::fromEuler({90, 90, 0});
    Vec3 v = rotate(q, {0, 0, 1});
    // X=90 turns +Z into -Y, then Y=90 leaves -Y unchanged.
    CHECK_NEAR(v.x, 0, 1e-5);
    CHECK_NEAR(v.y, -1, 1e-5);
    CHECK_NEAR(v.z, 0, 1e-5);
}

AVEN_TEST(math_decompose) {
    Vec3 t{1, 2, 3}, s{2, 1, 4};
    Quat r = Quat::fromEuler({15, 25, 35});
    Vec3 t2, s2;
    Quat r2;
    CHECK(decompose(Mat4::trs(t, r, s), t2, r2, s2));
    CHECK_NEAR(t2.y, 2, 1e-5);
    CHECK_NEAR(s2.z, 4, 1e-4);
    CHECK_NEAR(std::abs(dot(r, r2)), 1, 1e-4);
}

AVEN_TEST(math_projection) {
    Mat4 p = Mat4::perspective(radians(90), 1, 0.1f, 100);
    Vec4 nearPt = p * Vec4(0, 0, -0.1f, 1);
    Vec4 farPt = p * Vec4(0, 0, -100, 1);
    CHECK_NEAR(nearPt.z / nearPt.w, -1, 1e-4);
    CHECK_NEAR(farPt.z / farPt.w, 1, 1e-4);
    Mat4 view = Mat4::lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK_NEAR(transformPoint(view, {0, 0, 0}).z, -5, 1e-5);
}

AVEN_TEST(json_roundtrip) {
    std::string err;
    Json j = Json::parse(R"({"name": "Player", "pos": [1.5, -2, 3], "tags": ["a", "b"], "on": true,
        "nested": {"x": null, "s": "line\nbreak é"}})",
                         &err);
    CHECK(err.empty());
    CHECK_EQ(j["name"].asString(), std::string("Player"));
    CHECK_NEAR(j["pos"][0].asNumber(), 1.5, 1e-9);
    CHECK_EQ(j["tags"].size(), size_t(2));
    CHECK(j["on"].asBool());
    CHECK(j["nested"]["x"].isNull());
    CHECK(j["missing"]["deep"].isNull());
    Json back = Json::parse(j.dump(2), &err);
    CHECK(err.empty());
    CHECK(back == j);
    CHECK_EQ(back["nested"]["s"].asString(), std::string("line\nbreak \xc3\xa9"));
}

AVEN_TEST(json_errors_have_locations) {
    std::string err;
    Json::parse("{\n  \"a\": 1,\n  \"b\": }", &err);
    CHECK(err.find("line 3") != std::string::npos);
}

// Hand-edited files: // comments and trailing commas are forgiven.
AVEN_TEST(json_is_forgiving_about_hand_edits) {
    std::string err;
    Json j = Json::parse("{\n  // the player\n  \"speed\": 5,\n  \"list\": [1, 2,],\n}", &err);
    CHECK(err.empty());
    CHECK_NEAR(j["speed"].asNumber(), 5, 1e-9);
    CHECK_EQ(j["list"].size(), size_t(2));
}

AVEN_TEST(json_float_formatting) {
    Json j = Json(0.1f);
    CHECK_EQ(j.dump(), std::string("0.1"));
    CHECK_EQ(Json(42).dump(), std::string("42"));
    CHECK_EQ(Json(-3.25).dump(), std::string("-3.25"));
}

AVEN_TEST(json_preserves_key_order) {
    Json j = Json::object();
    j["zebra"] = 1;
    j["apple"] = 2;
    CHECK_EQ(j.dump(), std::string(R"({"zebra":1,"apple":2})"));
}

AVEN_TEST(uuid_string_roundtrip) {
    UUID id = UUID::generate();
    CHECK(static_cast<bool>(id));
    CHECK(UUID::fromString(id.toString()) == id);
}
