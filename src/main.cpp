#include "Viewer/Viewer.hpp"
#include "Converter/Converter.hpp"

#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using CommandHandler = std::function<int(const std::vector<std::string_view>&)>;
constexpr std::string_view usageMessage = R"(Usage:
  KelpEngine --help
  KelpEngine --view <path to .kelp file>
  KelpEngine --convert <path to .gltf/.glb file> <output .kelp path>
)";

namespace {
    int handleHelp(const std::vector<std::string_view>& /* UNUSED */) {
        std::cout << usageMessage << std::endl;
        return EXIT_SUCCESS;
    }

    int handleView(const std::vector<std::string_view>& args) {
        if (args.size() != 3) {
            std::cerr << "Error: --view requires exactly one argument: <path to .kelp file>" << std::endl << usageMessage << std::endl;
            return EXIT_FAILURE;
        }

        try {
            Viewer viewer;
            viewer.run(args[2]);
            return EXIT_SUCCESS;
        } catch (const std::exception& e) {
            std::cerr << "Viewer error: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }
    }

    int handleConvert(const std::vector<std::string_view>& args) {
        if (args.size() != 4) {
            std::cerr << "Error: --convert requires exactly two arguments: <input path> <output path>" << std::endl << usageMessage << std::endl;
            return EXIT_FAILURE;
        }

        try {
            Converter converter;
            converter.convert(args[2], args[3]);
            return EXIT_SUCCESS;
        } catch (const std::exception& e) {
            std::cerr << "Converter error: " << e.what() << std::endl;
            return EXIT_FAILURE;
        }
    }
}   // namespace

int main(int argc, char *argv[]) {
    const std::vector<std::string_view> args(argv, argv + argc);
    if (argc < 2) {
        std::cerr << usageMessage << std::endl;
        return EXIT_FAILURE;
    }

    const std::map<std::string_view, CommandHandler> command_handlers = {
        {"--help",    handleHelp},
        {"--view",    handleView},
        {"--convert", handleConvert}
    };

    try {
        const auto& handler_it = command_handlers.find(args[1]);
        if (handler_it != command_handlers.end())
            return handler_it->second(args);

        std::cerr << "Error: Unknown command: " << std::string(args[1]) << std::endl << usageMessage << std::endl;
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled Runtime Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unhandled catch-all error" << std::endl;
        return EXIT_FAILURE;
    }
}
