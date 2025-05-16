#pragma once

#include <string>

enum class ArchiveType {
    ZIP,
    JAR
};

struct ProgramArgs {
    ArchiveType thisArchiveType = ArchiveType::ZIP;
    std::string image_file;
    std::string archive_file;

    static ProgramArgs parse(int argc, char** argv);
};
