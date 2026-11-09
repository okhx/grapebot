
#include "label/rift/rift.hpp"

#include <cstdio>
#include <string>

using namespace grape::rift;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

static Context g_context;

static std::string render(const std::string& source) {
    const Script script = Script::compile(source);
    return script.run(g_context);
}

static void expect(const std::string& source, const std::string& want) {
    const std::string got = render(source);
    ++g_checks;
    if (got != want) {
        std::printf("FAIL: %s\n  want: %s\n  got:  %s\n", source.c_str(),
                    want.c_str(), got.c_str());
        ++g_failures;
    }
}

static void expectInvalid(const std::string& source) {
    const Script script = Script::compile(source);
    ++g_checks;
    if (script.valid()) {
        std::printf("FAIL: expected a parse error for: %s\n", source.c_str());
        ++g_failures;
    }
}

int main() {
    expect("2 + 2 = { 2 + 2 }", "2 + 2 = 4");
    expect("no expressions here", "no expressions here");
    expect("", "");

    g_context.set("progress", 12.34567);
    expect("Progress: { precision(progress, 5) }%", "Progress: 12.34567%");
    expect("{ precision(3.14159, 2) }", "3.14");
    expect("{ precision(3.0, 0) }", "3");

    expect("{ 1 }", "1");
    expect("{ 1.5 }", "1.5");
    expect("{ 3.0 }", "3");
    expect("{ \"Hello\" }", "Hello");
    expect("{ 'Single quotes also work' }", "Single quotes also work");
    expect("{ true }", "true");
    expect("{ false }", "false");
    expect("{ null }", "null");

    g_context.set("progress", Value(60.0));
    expect("{ progress >= 50 ? \"Halfway there!\" : \"Keep going!\" }",
           "Halfway there!");
    g_context.set("progress", Value(10.0));
    expect("{ progress >= 50 ? \"Halfway there!\" : \"Keep going!\" }",
           "Keep going!");
    expect("{ 1 > 2 }", "false");
    expect("{ 1 < 2 }", "true");
    expect("{ 2 >= 2 }", "true");
    expect("{ 2 <= 1 }", "false");
    expect("{ 2 == 2 }", "true");
    expect("{ 2 != 2 }", "false");
    expect("{ true + 1 }", "2");
    expect("{ false + 1 }", "1");

    expect("{ \"Epic number: \" + 100 }", "Epic number: 100");
    expect("{ \"Hello, \" + \"World\" }!", "Hello, World!");
    expect("{ \"Hello!\" * 3 }", "Hello!Hello!Hello!");
    expect("{ 3 * \"ab\" }", "ababab");
    expect("{ \"x\" * 0 }", "");
    expect("{ \"x\" * -2 }", "");
    expect("{ \"2 + 2 = \" + (2 + 2) }", "2 + 2 = 4");

    expect("{ 2 + 3 * 4 }", "14");
    expect("{ (2 + 3) * 4 }", "20");
    expect("{ 10 - 2 - 3 }", "5");
    expect("{ 12 / 4 / 3 }", "1");
    expect("{ 7 % 3 }", "1");
    expect("{ -3 + 5 }", "2");
    expect("{ !true }", "false");
    expect("{ !0 }", "true");

    expect("{ len(\"Hello\") }", "5");
    expect("{ substr(\"Hello World\", 0, 5) }", "Hello");
    expect("{ toUpper(\"hello\") }", "HELLO");
    expect("{ toLower(\"HELLO\") }", "hello");
    expect("{ trim(\"  hello  \") }", "hello");
    expect("{ replace(\"cats are cute\", \"cats\", \"dogs\") }",
           "dogs are cute");
    expect("{ round(3.14159) }", "3");
    expect("{ round(3.7) }", "4");
    expect("{ floor(3.9) }", "3");
    expect("{ ceil(3.1) }", "4");
    expect("{ abs(-5) }", "5");
    expect("{ min(1, 3, 5) }", "1");
    expect("{ max(1, 3, 5) }", "5");
    expect("{ sum(1, 2, 3) }", "6");
    expect("{ avg(1, 2, 3) }", "2");
    expect("{ sqrt(16) }", "4");
    expect("{ pow(2, 3) }", "8");
    expect("{ round(sin(3.14159 / 2)) }", "1");
    expect("{ leftPad(\"123\", 5, '0') }", "00123");
    expect("{ rightPad(\"123\", 5, '0') }", "12300");
    expect("{ middlePad(\"1\", 5, '-') }", "--1--");
    expect("{ leftPad(\"123\", 5) }", "  123");
    expect("{ leftPad(\"123456\", 3, '0') }", "123456");

    g_context.m_config = [](const std::string& key) -> Value {
        if (key == "creator.leveledit") return Value(true);
        if (key == "render.fps") return Value(60);
        return {};
    };
    expect("{ cfg(\"creator.leveledit\") }", "true");
    expect("{ cfg(\"render.fps\") }", "60");
    expect("{ cfg(\"nope\") }", "null");

    g_context.set("modVersion", Value("v.1.0.0-alpha.1"));
    g_context.set("platform", Value("Windows"));
    g_context.set("fps", Value(59.94));
    expect("{ modVersion } on { platform }", "v.1.0.0-alpha.1 on Windows");
    expect("{ round(fps) } FPS", "60 FPS");
    expect("[{ totallyUnknownVariable }]", "[null]");

    {
        Dict letters;
        letters["h"] = Value("H1");
        letters["e"] = Value("E1");
        letters["l"] = Value("L1");
        letters["o"] = Value("O1");
        Dict digits;
        digits["0"] = Value("Z");
        digits["1"] = Value("O");
        letters["num"] = Value(digits);
        g_context.set("ri", Value(letters));
    }
    expect("{ ri['h'] + ri['e'] + ri['l'] + ri['l'] + ri['o'] }",
           "H1E1L1L1O1");
    expect("{ ri.num[0] + ri.num[1] }", "ZO");
    expect("{ ri.missing }", "null");

    g_context.set("coins", Value(Array{Value(0), Value(2), Value(1)}));
    expect("{ coins[0] }{ coins[1] }{ coins[2] }", "021");
    expect("{ len(coins) }", "3");
    expect("{ coins[9] }", "null");
    expect("{ coins[-1] }", "null");
    expect("{ max(coins) }", "2");

    {
        Dict paths;
        paths["fire"] = Value(120);
        paths["ice"] = Value(40);
        g_context.set("paths", Value(paths));
        g_context.set("currentPath", Value("fire"));
    }
    expect("{ paths[\"fire\"] }", "120");
    expect("{ paths.ice }", "40");
    expect("{ paths[currentPath] }", "120");

    g_context.set("levelName", Value("Stereo Madness"));
    g_context.set("author", Value("RobTop"));
    g_context.set("attempt", Value(17));
    g_context.set("progress", Value(43.216));
    g_context.set("isPlatformer", Value(false));
    g_context.set("bestPercent", Value(91));
    expect("{ levelName } by { author }", "Stereo Madness by RobTop");
    expect("Attempt { attempt }", "Attempt 17");
    expect("{ precision(progress, 2) }% (best { bestPercent }%)",
           "43.22% (best 91%)");
    expect("{ isPlatformer ? \"time\" : \"progress\" }: "
           "{ precision(progress, 1) }",
           "progress: 43.2");
    g_context.set("cps", Value(14));
    expect("{ cps > 20 ? \"fast\" : cps > 10 ? \"ok\" : \"slow\" }", "ok");

    g_context.set("zero", Value(0));
    expect("{ zero != 0 && 10 / zero > 1 }", "false");
    expect("{ zero == 0 || 10 / zero > 1 }", "true");
    expect("{ 10 / zero }", "0");
    expect("{ 10 % zero }", "0");

    expect("a{}b", "ab");
    expect("a{  }b", "ab");
    expect("no closing", "no closing");
    expectInvalid("{ 1 + }");
    expectInvalid("{ unmatched");
    expectInvalid("{ \"unterminated }");
    expectInvalid("{ 1 ? 2 }");
    expectInvalid("{ (1 + 2 }");
    expectInvalid("{ foo[1 }");
    expectInvalid("{ 1 @ 2 }");
    expectInvalid("{ foo. }");
    expect("[{ someNewFunction(1, 2) }]", "[null]");
    expect("\\{ not an expression \\}", "{ not an expression }");
    expect("{ \"a{b}c\" }", "a{b}c");

    {
        const std::string out = render("{ \"ab\" * 100000000 }");
        check(out.size() <= 8192 + 16,
              "a huge string repeat is bounded rather than hanging");
    }

    {
        const Script script =
            Script::compile("{ levelName } { attempt } { levelName }");
        check(script.valid(), "a valid script reports valid");
        check(script.referenced().size() == 2,
              "each referenced name is listed once");
    }
    {
        const Script script = Script::compile("plain text");
        check(script.referenced().empty(), "plain text references nothing");
    }

    {
        const Script script = Script::compile("{ attempt }");
        Context first;
        first.set("attempt", Value(1));
        Context second;
        second.set("attempt", Value(2));
        check(script.run(first) == "1" && script.run(second) == "2",
              "one compiled script serves many evaluations");
    }

    {
        Context context;
        context.m_randomState = 12345;
        const Script script = Script::compile("{ random(1, 10) }");
        for (int i = 0; i < 200; ++i) {
            const double value = std::strtod(script.run(context).c_str(),
                                             nullptr);
            check(value >= 1.0 && value <= 10.0, "random stays in range");
            if (g_failures) break;
        }
    }

    if (g_failures) {
        std::printf("\nrift: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("rift: %d checks passed\n", g_checks);
    return 0;
}
