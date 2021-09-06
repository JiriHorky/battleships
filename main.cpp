#include <bits/stdc++.h>
#include <assert.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <math.h>


#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

using namespace std;

typedef long long ll;

// CONSTANTS
// board dimensions
#define WIDTH 9
#define HEIGHT 8
#define SIZE WIDTH*HEIGHT


#define ITERS 10000000
//#define ITERS 1000


ll frequencies[WIDTH*HEIGHT];
unsigned long long hits  = 0;


class ship {
	public:
		ship(const char * name, unsigned char width, unsigned char height, const vector<bool> shape) {
			unsigned int h, w;

			assert(shape.size() == width * height);

			//TODO this code doesn't check for the boat's cell to contact correctly

			//in each column, there must at least one "1" on any row
			vector<bool> w_validator;
			vector<bool> h_validator;

			for (w=0; w<width; w++) {
				w_validator.push_back(false);
			}
			for (h=0; h<height; h++) {
				h_validator.push_back(false);
			}

			for(h = 0; h<height; h++) {
				for (w=0; w<width; w++) {
					if( shape[h*width + w] == true) {
						h_validator[h] =true;
						w_validator[w] = true;
					}
				}
			}

			for (w=0; w<width; w++) {
				if (w_validator[w] == false) {
					fprintf(stderr, "Invalid shape passed, validation of width failed on w=%d\n", w);
					assert(0);
				}

			}
			for (h=0; h<height; h++) {
				if (h_validator[h] == false) {
					fprintf(stderr, "Invalid shape passed, validation of width failed on h=%d\n", h);
					assert(0);
				}
			}

			this->shape = shape;
			this->width = width;
			this->height = height;

			this->surr_width = width+2;
			this->surr_height = height+2;

			surr_shape.resize(surr_width*surr_height);

			//copy shape to surr_shape starting from second row, second column:
			for(h = 0; h < height; h++) {
				for (w=0; w < width; w++) {
					surr_shape[(h+1)*surr_width + 1 + w ] = shape[h*width + w];
				}
			}

			//go over the ship's shape - there is a ship that touches us - we can only check on the right, botoom and right-bottom side
			unsigned int p; //pos
			for (p = 0; p < shape.size(); p++) {
				if (shape[p]) {
					int p2 = to_surr_index(p); 
					int w = p2 % surr_width;
					int h = p2 / surr_width;

					surr_shape[(h-1)*surr_width + (w-1)] = 1; //top-left corner
					surr_shape[(h-1)*surr_width + w] = 1; //top
					surr_shape[(h-1)*surr_width + w+1] = 1; //top-right corner
					surr_shape[(h)*surr_width + (w-1)] = 1; //left
					surr_shape[p2] = //the corresponding cell
					surr_shape[(h)*surr_width + (w+1)] = 1; //right
					surr_shape[(h+1)*surr_width + (w-1)] = 1; //bottom-left corner
					surr_shape[(h+1)*surr_width + w] = 1; //bottom
					surr_shape[(h+1)*surr_width + w+1] = 1; //bottom-right corner
				}
			}

			this->name = name;
			this->pos = -1;
			_shape_map_index = -1;
			_compute_shape_hash();
		} //end of contructor

		bool is_ship_shape(const int dx, const int dy) {
			return shape[dy * width + dx ];	
		}

		bool is_ship_surr_shape(const int dx, const int dy) {
			return surr_shape[dy * surr_width + dx ];	
		}

		void set_shape_map_index(const size_t i) {
			_shape_map_index = i;
		}

		size_t get_shape_map_index() const {
			return _shape_map_index;
		}

		void set_pos(const int grid_pos) {
			this->pos = grid_pos;

			//compute all the grid position this ship is on
			int h = grid_pos / WIDTH;
			int w = grid_pos % WIDTH;

			grid_positions.clear();

			if (pos == -1) { //unplaced
				return;
			}

			for (unsigned int i = 0; i < shape.size(); i++) {
				int sh = i / width;
				int sw = i % width;

				if (shape[i]) {
					this->grid_positions.push_back((h+sh)*WIDTH + (w+sw));
				}
			}
		}

		const vector<int> & get_grid_positions() const {
			return grid_positions;
		}

		bool is_on_pos(const int grid_pos) const {
			for (const auto & p: grid_positions) {
				if (p > grid_pos) {
					return false;
				}
				if (p == grid_pos) {
					return true;
				}
			}
			return false;
		}

		int fits_in_grid(const int pos) const {
			if ( (pos % WIDTH + width) > WIDTH ) {
				return 0;
			}
			if ( (pos / WIDTH + height) > HEIGHT ) {
				return 0;
			}
			return 1;
		}

		size_t shape_hash() const {
			return _shape_hash;
		}



		string name;
		vector<bool> shape;
		vector<bool> surr_shape;

		vector<int> grid_positions;

		// boundind box of the ship
		unsigned char width;
		unsigned char height;

		// boundind box of the ship to indicate it can not be placed
		unsigned char surr_width;
		unsigned char surr_height;

		//placement on the grid
		int pos;



					private:
		size_t _shape_hash;
		size_t _shape_map_index;

		//transpose shape's index to surr_shape index if the "shape" box is placed in the middle of the "surr_shape".
		int to_surr_index(int idx) {
			int h = idx / width;
			int w = idx % width;

			return (h+1)*surr_width + w + 1;
		}

		//transpose position on the grid of the surrounding shape to position on the grid of the shape itself (ship itself)
		int to_normal_grid_position(int surr_pos) {
			int h = surr_pos / WIDTH;
			int w = surr_pos % WIDTH;

			return (h+1)*WIDTH+ w + 1;
		}

		void print_surr_shape() {
				cout << "The surr_shape is:" << endl;
				int i = 0;
				ostringstream s1, s2;
				for (const auto& b : surr_shape) {
					s1 << i << " " ;
					s2 << b << " " ;
					i += 1; 
				}
				cout << s1.str() << endl;
				cout << s2.str() << endl;
		}

		void _compute_shape_hash() {
			size_t h = 0;
			int i = 0;

			h |= 1 << shape.size();
			for(const auto& idx : shape) {
					h |= idx << (int)i;
					i++;
			}

			_shape_hash = h;
		}
};

typedef ship ship_t;

class shapes_map {
	public:
		shapes_map(vector<ship_t *> ships) {
			std::set<size_t> hashes;
			std::vector<size_t> hash_pairs;

			//get unique hashes
			for (const auto & ship: ships) {
				hashes.insert(ship->shape_hash());
			}

			for (const auto & hash: hashes) {
				hash_pairs.push_back(hash);
				_shapes_placement.push_back(0);
			}

			for (auto & ship: ships) {
				size_t idx = 0;
				for (idx=0; idx < hash_pairs.size(); idx++) {
					if (ship->shape_hash() == hash_pairs[idx]) {
						ship->set_shape_map_index(idx);
						break;
					}
				}
			}

			size_t i = 0;
			for (i=0; i < hash_pairs.size(); i++) {
				cout << i << " " << hash_pairs[i] << endl;
			}

		}

		const int & operator[] (const size_t & key ) const {
			return _shapes_placement[key];
		};

		int & operator[] (const size_t & key ) {
			return _shapes_placement[key];
		};

	private:
		std::vector<int> _shapes_placement;

};


vector<ship_t *> global_ships; 

shapes_map shapes_placed(global_ships);

typedef std::bitset<SIZE> grid_t;

/*
 * grid bits representation 4x3
  -----------------
  | 0 | 1 | 2 | 3 |
  | 4 | 5 | 6 | 7 | 
  | 8 | 9 | 10| 11|
  ----------------
*/

template <typename T>
void print_grid(const T grid, bool print_numbers=false) {
	int i, j;
	int pos;
	int num_digits = 1;

	if (print_numbers) {
		num_digits = log10(SIZE)+1;
	} else {
		int max = -1;
		for (i=0; i< SIZE; ++i) {
			if (grid[i] > max) { max = grid[i]; }
		}
		if (max <= 0 ) {
			num_digits = 1;
		} else {
			num_digits = log10(max)+1;
		}
	}

	int table_width= WIDTH*(num_digits+1) + 1;

	for (i=0; i< table_width; i++) {
		printf("-");
	}

	for (i=0; i< HEIGHT; i++) {
		printf("\n");
		printf("|");
		for (j=0; j<WIDTH; j++) {
			pos = i*WIDTH+j;
			unsigned long long c;
			if (print_numbers) {
				c = pos;
			} else {
				c = grid[pos];
			}
			printf("%*lld|", num_digits, c);
		}
	}
	printf("\n");
	for (i=0; i< table_width; i++) {
		printf("-");
	}
	printf("\n");
}

void print_grid_with_ships(const grid_t grid, vector<ship_t*> ships) {
	int i, j;
	int pos;

	int num_digits = log10(SIZE)+1;

	int table_width= WIDTH*(num_digits+1) + 1;

	for (i=0; i< table_width; i++) {
		printf("-");
	}

	for (i=0; i< HEIGHT; i++) {
		printf("\n");
		printf("|");
		for (j=0; j<WIDTH; j++) {
			pos = i*WIDTH+j;
			bool is_ship = false;

			for (const auto& ship : ships) {
				if (ship->is_on_pos(pos)) {
					printf(ANSI_COLOR_RED "%*d" ANSI_COLOR_RESET "|", num_digits, 2);
					is_ship = true;
					break;
				}
			}
			if (! is_ship) {
				printf("%*d|", num_digits, grid[pos]);
			}
		}
	}
	printf("\n");
	for (i=0; i< table_width; i++) {
		printf("-");
	}
	printf("\n");
}

//places ship's shape

int place_ship(grid_t & grid, const int & pos, ship_t & ship) {
	int sh, sw;
	int p;

	if ( ! ship.fits_in_grid(pos) ) {
		return 0;
	}

	int w = pos % WIDTH;
	int h = pos / WIDTH;

	for (sh = 0; sh < ship.height; sh++) {
		for (sw = 0; sw < ship.width; sw++) {
			if ( ship.is_ship_shape(sw, sh)) {
				p=(h+sh)*WIDTH + w + sw;
				if (grid[p] == 1 ) {
					return 0;
				}
				grid[p] = 1;
			}
		}
	}

	ship.set_pos(pos);

  //now mark even surrounding of the ship as used
  h = h - 1;
  w = w - 1;
	for (sh = 0; sh < ship.surr_height; sh++) {
		for (sw = 0; sw < ship.surr_width; sw++) {
			if ( (h+sh) >= HEIGHT || (h+sh) < 0 || (w+sw) >= WIDTH || (w+sw) < 0 ) {
				continue;
			}
			p = (h+sh)*WIDTH + (w + sw);
			if ( ship.is_ship_surr_shape(sw, sh)) {
				grid[p] = 1;
			}
		}
	}

	return 1;
}


void add_frequencies(ll frequencies[], const vector<ship_t *> & ships) {
	for (const auto & ship : ships) {
		for (const auto & pos: ship->get_grid_positions()) {
			frequencies[pos] += 1;
		}			
	}
}




void place_ships(grid_t grid, vector<ship_t *> ships) {
	int idx, prev_idx;

	//start_position 
	grid_t g2 = ~grid;
	idx = g2._Find_first();

	ship_t * ship = ships.front();
	ships.erase(ships.begin());

	const size_t h = ship->get_shape_map_index();

	while (idx < SIZE) { 
		assert( ship->pos == -1 ); //ship not placed yet
		if ( ship->width == 5 ) {
			   auto timenow =  chrono::system_clock::to_time_t(chrono::system_clock::now());
    			cout << "Placing ship on idx " << idx << " " << ctime(&timenow);
		}

		//if similar shape is not already placed
		if ( idx >= shapes_placed[h]) {
	//		printf("placing ship: %s on idx: %d\n", ship->name.c_str(), idx);
			grid_t old_grid = grid;
			if (place_ship(grid, idx, *ship)) { //we managed to place the ship there, yes!
//				printf("Managed to place ship: %s on pos: %d\n", ship->name.c_str(), idx);	
				prev_idx = shapes_placed[h];
				shapes_placed[h] = idx;

				if (ships.size() == 0) { //Note: we don't check for this case in the beginning of the place_ships function to safe some function calls
					add_frequencies(frequencies, global_ships);
					//		print_grid_with_ships(grid, global_ships);
					hits += 1;
				} else {
					//recurse
					place_ships(grid, ships);
				}
				//unplace ship
//				printf("Unplacing: %s from pos: %d\n", ship->name.c_str(), idx);	
				shapes_placed[h] = prev_idx;
				ship->set_pos(-1);
			}
			grid = old_grid;
		} 
		g2 = ~grid;
		idx = g2._Find_next(idx);
	}
}


ll count_frequencies(grid_t &grid, vector<ship_t *> ships) {
	for (auto ship : ships) {
		//do nothing
	}
	return 0;
}


int main(int argc, char * argv[]) {
	grid_t grid;

	ship_t jednicka1("jednicka1", 1, 1, vector<bool> {1} );
	ship_t jednicka2("jednicka2", 1, 1, vector<bool> {1} );
	ship_t jednicka3("jednicka3", 1, 1, vector<bool> {1} );
	ship_t jednicka4("jednicka4", 1, 1, vector<bool> {1} );
	ship_t dvojka1("dvojka1", 2, 1, vector<bool> {1,1} );
	ship_t dvojka2("dvojka2", 2, 1, vector<bool> {1,1} );
	ship_t dvojka3("dvojka3", 2, 1, vector<bool> {1,1} );
	ship_t trojka1("trojka1", 3, 1, vector<bool> {1,1,1} );
	ship_t trojka2("trojka2", 3, 1, vector<bool> {1,1,1} );
	ship_t ctyrka("ctyrka", 4, 1, vector<bool> {1,1,1,1} );
	ship_t petka("petka", 5, 1, vector<bool> {1,1,1,1,1} );
	ship_t parnik1("parnik1", 3, 2, vector<bool> {0,1,0,1,1,1} );
	ship_t parnik2("parnik2", 3, 2, vector<bool> {0,1,0,1,1,1} );
	ship_t kriznik("kriznik", 5, 2, \
			vector<bool> \
			{0,1,0,1,0,\
			 1,1,1,1,1}\
		  	);
	vector<ship_t *> ships;
//	ships.push_back(&kriznik);
/*
	ships.push_back(&parnik1);
	ships.push_back(&parnik2);
	*/
	ships.push_back(&petka);
	ships.push_back(&ctyrka);
	ships.push_back(&trojka1);
	ships.push_back(&trojka2);
//	ships.push_back(&dvojka1);
//	ships.push_back(&dvojka2);
//	ships.push_back(&dvojka3);
	ships.push_back(&jednicka1);
	ships.push_back(&jednicka2);
	ships.push_back(&jednicka3);
//	ships.push_back(&jednicka4);
//
	global_ships = ships;

//	grid[55] = 1;
	print_grid(grid, true);
//	place_ship(grid, 2, dvojka1);
//	place_ship(grid, 10, dvojka2);
//	print_grid_with_ships(grid, global_ships);
	shapes_placed = shapes_map(global_ships);
	bzero(frequencies, SIZE*sizeof(ll));

	place_ships(grid, ships);
	print_grid(frequencies);
	printf("Total hits: %llu\n", hits);
}
