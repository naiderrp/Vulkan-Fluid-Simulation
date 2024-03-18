#include "render_system.hpp"

int main(int argc, char** argv){
    try {
        render_system app{};
        app.run();
    }
    catch (std::runtime_error& e) {
        std::cout << e.what();
    }
}
