#include "config.hpp"

struct fluid {
	static auto generate_initial_positions(int num, float radius = 0.005f) {
		std::vector<glm::vec2> initial_positions(num); 

		for (int i = 0, x = 0, y = 0; i < num; ++i) {
			initial_positions[i].x = -0.625f + radius * 2 * x;
			initial_positions[i].y = -1 + radius * 2 * y;
			
			++x;

			if (x >= 125) {
				x = 0;
				++y;
			}
		}

		return initial_positions;
	}
};