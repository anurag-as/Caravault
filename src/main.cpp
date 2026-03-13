#include <iostream>
#include <CLI/CLI.hpp>

int main(int argc, char** argv) {
    CLI::App app{"Caravault - Offline Multi-Drive File Synchronization System"};
    
    // Version flag
    app.set_version_flag("--version", "1.0.0");
    
    // Subcommands will be added in later tasks
    // - sync: Synchronize all connected drives
    // - status: Show status of connected drives
    // - conflicts: List detected conflicts
    // - resolve: Manually resolve conflicts
    // - scan: Rebuild manifest for a drive
    // - verify: Verify data integrity
    // - config: Configure system settings
    
    CLI11_PARSE(app, argc, argv);
    
    std::cout << "Caravault v1.0.0 - Build infrastructure ready" << std::endl;
    std::cout << "Run 'caravault --help' for available commands" << std::endl;
    
    return 0;
}
