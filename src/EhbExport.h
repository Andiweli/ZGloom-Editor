#pragma once

#include <filesystem>
#include <string>

namespace EhbExport
{
    struct Result
    {
        bool success = false;
        int generatedPictures = 0;
        int copiedFiles = 0;
        int roundtripChecks = 0;
        int warnings = 0;
        int errors = 0;
        std::filesystem::path reportFile;
        std::filesystem::path outputImage;
        std::filesystem::path outputPalette;
        std::wstring message;
    };

    Result ExportReferenceSet(const std::filesystem::path& gameRoot,
        const std::filesystem::path& outputRoot);

    Result ExportRetailPictureSet(const std::filesystem::path& gameRoot,
        const std::filesystem::path& pictureDir,
        const std::filesystem::path& outputRoot,
        bool composeGloomTitleBrush);

    Result ExportFastTitlePackage(const std::filesystem::path& gameRoot,
        const std::filesystem::path& titleSource,
        const std::filesystem::path& brushSource,
        const std::filesystem::path& outputRoot);

    Result ConvertImageFile(const std::filesystem::path& sourceFile,
        const std::filesystem::path& outputFolder);
}
