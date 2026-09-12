// Developer tool: translates ARB vertex/fragment program text into GLSL ES with the same
// translator the Android device uses, so the output can be validated offline. Usage:
//
//   ArbTool <arb text file> <output prefix>
//
// The input may hold several programs separated by lines starting with "#----"; each is
// written to <prefix>_<n>.vert or .frag.

#include "gx/gles/ArbToGlsl.hpp"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: ArbTool <arb text file> <output prefix>\n");
        return 1;
    }

    FILE* in = fopen(argv[1], "rb");

    if (!in) {
        printf("could not open %s\n", argv[1]);
        return 1;
    }

    std::string text;
    char buf[4096];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        text.append(buf, n);
    }

    fclose(in);

    // Split into programs on the permutation separator
    std::vector<std::string> programs;
    size_t pos = 0;

    while (pos < text.size()) {
        size_t start = text.find("!!ARB", pos);

        if (start == std::string::npos) {
            break;
        }

        size_t next = text.find("#----", start);
        size_t end = next == std::string::npos ? text.size() : next;

        programs.push_back(text.substr(start, end - start));
        pos = end;
    }

    int32_t failures = 0;

    for (size_t i = 0; i < programs.size(); i++) {
        std::string glsl;
        ArbProgramInfo info;

        if (!ArbToGlsl(programs[i].c_str(), programs[i].size(), glsl, info)) {
            printf("%s[%zu]: %s\n", argv[1], i, info.error.c_str());
            failures++;
            continue;
        }

        std::string path = std::string(argv[2]) + "_" + std::to_string(i) + (info.fragment ? ".frag" : ".vert");
        FILE* out = fopen(path.c_str(), "wb");

        if (!out) {
            printf("could not create %s\n", path.c_str());
            return 1;
        }

        fwrite(glsl.data(), 1, glsl.size(), out);
        fclose(out);
    }

    printf("%zu programs, %d failures\n", programs.size(), failures);

    return failures ? 1 : 0;
}
