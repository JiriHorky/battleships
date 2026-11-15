// Interactive terminal UI: mark shots on the board, see the hit probabilities
// recomputed after every change.
//
// usage: tui [--fleet game|czech|legacy] [--size WxH] [--no-rotate] [--no-sunk-announce] [--threads N] [state-file]
//
// Keys:
//   arrows / hjkl   move                 x  hit           o  miss
//   s               sunk: turns the hit group under the cursor into a sunk ship
//                   (or back into hits)
//   space . del     clear cell           b / enter  jump to the best shot
//   u               undo                 r  reset board
//   w               save to the state file (same format as bin/play reads)
//   q               quit
//
// Sinking is assumed to be announced (--no-sunk-announce drops this): a hit
// not marked sunk belongs to a ship with an unhit cell left. A hit group that
// can only be a whole ship is therefore sunk, and is marked so automatically.
//
// The solver runs in a background thread, so the cursor keeps moving while a
// slow position (e.g. the empty 10x10 board, ~0.5 s) is being computed.
#include <poll.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <sstream>

#include "board.h"
#include "dp.h"
#include "fleets.h"

using namespace bs;

// ---------------------------------------------------------------- terminal

static struct termios g_orig;
static bool g_raw = false;

static void term_write(const std::string & s) {
	size_t off = 0;
	while (off < s.size()) {
		ssize_t n = write(STDOUT_FILENO, s.data() + off, s.size() - off);
		if (n <= 0) return;
		off += (size_t)n;
	}
}

static void term_restore() {
	if (!g_raw) return;
	g_raw = false;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
	term_write("\x1b[0m\x1b[?25h\x1b[?1049l");  // reset colours, show cursor, leave alt screen
}

static void on_signal(int sig) {
	term_restore();
	signal(sig, SIG_DFL);
	raise(sig);
}

static void term_raw() {
	tcgetattr(STDIN_FILENO, &g_orig);
	struct termios t = g_orig;
	t.c_lflag &= ~(ICANON | ECHO | IEXTEN);
	t.c_iflag &= ~(IXON | ICRNL);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
#ifdef VSUSP
	t.c_cc[VSUSP] = _POSIX_VDISABLE;  // ctrl-Z would leave the terminal raw
#endif
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
	g_raw = true;
	atexit(term_restore);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);
	signal(SIGQUIT, on_signal);
	term_write("\x1b[?1049h\x1b[?25l\x1b[2J");  // alt screen, hide cursor, clear
}

enum Key { K_NONE = -1, K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT };

// Returns K_NONE if nothing arrives within timeout_ms.
static int read_key(int timeout_ms) {
	struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
	if (poll(&pfd, 1, timeout_ms) <= 0) return K_NONE;
	unsigned char c;
	if (read(STDIN_FILENO, &c, 1) != 1) return 'q';  // stdin closed
	if (c != 27) return c;
	// escape sequence: ESC [ A..D (or ESC O A..D)
	unsigned char seq[2];
	for (int i = 0; i < 2; i++) {
		if (poll(&pfd, 1, 30) <= 0 || read(STDIN_FILENO, &seq[i], 1) != 1) return K_NONE;
	}
	if (seq[0] != '[' && seq[0] != 'O') return K_NONE;
	switch (seq[1]) {
		case 'A': return K_UP;
		case 'B': return K_DOWN;
		case 'C': return K_RIGHT;
		case 'D': return K_LEFT;
		case '3':  // delete key: ESC [ 3 ~
			if (poll(&pfd, 1, 30) > 0 && read(STDIN_FILENO, &c, 1) != 1) return K_NONE;
			return 127;
	}
	return K_NONE;
}

// ---------------------------------------------------------------- solving

struct Outcome {
	uint64_t gen = 0;
	bool ok = false;
	std::string error;  // set if !ok
	Problem p;
	Result r;
	double secs = 0;
	// Hit groups that are whole ships in every configuration; set only when
	// that is why no configuration fits (they would have been announced sunk).
	std::vector<std::vector<int>> sink;
	std::string hint;  // shown with the error
};

// Hit groups (8-connected x cells, always one ship) with no ship cell next to
// them in any configuration of r: each is exactly one whole ship.
static std::vector<std::vector<int>> whole_ships(const Board & b, const Result & r) {
	std::vector<std::vector<int>> out;
	std::vector<uint8_t> seen(b.W * b.H, 0);
	for (int i = 0; i < b.W * b.H; i++) {
		if (b.cells[i] != 'x' || seen[i]) continue;
		std::vector<int> g = component(b, i);
		Mask m = 0;
		for (int x : g) {
			seen[x] = 1;
			m |= bit(x);
		}
		Mask around = halo_of(b.W, b.H, m) & ~m;
		bool closed = true;
		for (int x = 0; x < b.W * b.H && closed; x++)
			if ((around & bit(x)) && r.cell_counts[x] != 0) closed = false;
		if (closed) out.push_back(g);
	}
	return out;
}

static Outcome compute(uint64_t gen, Board b, Fleet fleet, bool rotate, bool announced, DpOptions opt) {
	Outcome o;
	o.gen = gen;
	try {
		auto t0 = std::chrono::steady_clock::now();
		GameState gs = board_to_state(b, fleet, rotate);
		o.p = make_problem(gs, fleet, rotate, announced);
		o.r = solve_dp(o.p, nullptr, opt);
		if (o.r.total == 0 && announced && !o.p.infeasible) {
			// Without the rule, which hit groups are certainly whole ships?
			Result loose = solve_dp(make_problem(gs, fleet, rotate, false), nullptr, opt);
			if (loose.total) o.sink = whole_ships(b, loose);
			if (loose.total && o.sink.empty()) o.hint = "if a ship was announced sunk, press s on it";
		}
		o.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		o.ok = o.r.total != 0;
		if (!o.ok)
			o.error = "no fleet configuration fits this board" +
			          (o.p.infeasible ? ": " + o.p.infeasible_reason : std::string()) +
			          (o.hint.empty() ? std::string() : " (" + o.hint + ")");
	} catch (const std::exception & e) {
		o.error = e.what();
	}
	return o;
}

// ---------------------------------------------------------------- UI

static std::string col_name(int c) {
	return c < 26 ? std::string(1, (char)('A' + c)) : std::to_string(c + 1);
}

struct App {
	Fleet fleet;
	bool rotate = true;
	bool announced = true;  // sinking is announced (see the top of the file)
	DpOptions opt;
	std::string file;
	bool square = true;     // square cells (falls back to compact if the terminal is short)
	bool file_ours = false;  // file was given on the command line or written by us

	Board board;
	std::vector<Board> undo;
	int cr = 0, cc = 0;

	uint64_t gen = 0;          // bumped on every board change
	Outcome last;              // newest finished computation
	bool have_last = false;
	std::future<Outcome> job;  // running computation, if valid()
	std::string message;
	std::string shape_error;   // board cannot even be interpreted (bad sunk group)
	std::vector<int> sunk_count;  // per fleet type, from the current board

	bool computing() const { return job.valid(); }
	std::string cell_name(int i) const { return col_name(i % board.W) + std::to_string(i / board.W + 1); }
	bool fresh() const { return have_last && last.gen == gen; }

	void start_job() {
		try {
			job = std::async(std::launch::async, compute, gen, board, fleet, rotate, announced, opt);
		} catch (const std::exception & e) {
			message = std::string("cannot start the solver: ") + e.what();
		}
	}

	// Called after every board edit.
	void changed() {
		gen++;
		shape_error.clear();
		try {
			GameState gs = board_to_state(board, fleet, rotate);
			sunk_count.assign(fleet.size(), 0);  // kept from the last valid board on error
			for (const auto & s : gs.sunk) sunk_count[s.type]++;
		} catch (const std::exception & e) {
			shape_error = e.what();
			return;  // nothing to solve until the board makes sense
		}
		if (!computing()) start_job();  // otherwise restarted when the running job ends
	}

	void edit(void (*fn)(App &)) {
		undo.push_back(board);
		if (undo.size() > 1000) undo.erase(undo.begin());
		message.clear();
		fn(*this);
		if (board.cells == undo.back().cells) undo.pop_back();  // no-op edit
		else changed();
	}

	// Poll the background job; start the next one if the board changed meanwhile.
	bool poll_job() {
		if (!computing() || job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
		last = job.get();
		have_last = true;
		if (last.gen == gen && !last.sink.empty()) {
			// Part of the edit that completed the ship, so no undo entry: undo
			// goes back to before that hit.
			std::string where;
			for (const auto & g : last.sink) {
				for (int x : g) board.cells[x] = 'S';
				where += (where.empty() ? "" : ", ") + cell_name(g[0]);
			}
			message = "marked sunk, only a whole ship fits there: " + where;
			changed();
			return true;
		}
		if (last.gen != gen && shape_error.empty()) start_job();
		return true;
	}

	int best_cell() const {
		if (!fresh() || !last.ok) return -1;
		int best = -1;
		for (int i = 0; i < board.W * board.H; i++)
			if (board.cells[i] == '.' && (best < 0 || last.r.cell_counts[i] > last.r.cell_counts[best])) best = i;
		if (best >= 0 && last.r.cell_counts[best] == 0) return -1;
		return best;
	}
};


// heat ramp (xterm-256 backgrounds), cold to hot
static const int RAMP[] = {236, 237, 23, 24, 30, 36, 72, 108, 143, 179, 215, 209, 203, 197};
static const int NRAMP = sizeof(RAMP) / sizeof(RAMP[0]);

// Fleet panel: every ship drawn by shape, afloat copies first (bright), sunk
// copies after (dark red). Returns lines and their visible width.
static std::vector<std::string> fleet_panel(const App & a, int & width) {
	std::vector<std::string> lines;
	std::vector<int> vis;  // visible width of each line
	auto add = [&](const std::string & text, int w) {
		lines.push_back(text);
		vis.push_back(w);
	};
	int ships = 0, cells = 0, hits = 0;
	for (char ch : a.board.cells) hits += ch == 'x';
	std::vector<int> left(a.fleet.size()), sunk(a.fleet.size());
	for (size_t t = 0; t < a.fleet.size(); t++) {
		sunk[t] = t < a.sunk_count.size() ? a.sunk_count[t] : 0;
		left[t] = std::max(0, a.fleet[t].count - sunk[t]);
		ships += left[t];
		cells += left[t] * a.fleet[t].shape.size();
	}
	char buf[96];
	snprintf(buf, sizeof buf, "Ships afloat: %d", ships);
	add(std::string("\x1b[1m") + buf + "\x1b[0m", (int)strlen(buf));
	snprintf(buf, sizeof buf, "Cells to hit: %d", std::max(0, cells - hits));
	add(std::string("\x1b[1m") + buf + "\x1b[0m", (int)strlen(buf));
	add("", 0);
	for (size_t t = 0; t < a.fleet.size(); t++) {
		const ShipType & st = a.fleet[t];
		std::string name = st.name;
		name.resize(std::max<size_t>(name.size() + 1, 9), ' ');
		std::string text = left[t] ? std::to_string(left[t]) + " left" : "all sunk";
		std::string sunk_txt = left[t] && sunk[t] ? ", " + std::to_string(sunk[t]) + " sunk" : "";
		std::string style = left[t] ? "\x1b[1m" : "\x1b[2m";
		add(style + name + "\x1b[0m" + (left[t] ? "" : "\x1b[38;5;203m") + text + "\x1b[0m" +
		        "\x1b[38;5;203m" + sunk_txt + "\x1b[0m",
		    (int)(name.size() + text.size() + sunk_txt.size()));
		int copies = left[t] + std::min(sunk[t], st.count);
		for (int r = 0; r < st.shape.h; r++) {
			std::string row = "  ";
			int w = 2;
			for (int k = 0; k < copies; k++) {
				if (k) {
					row += " ";
					w++;
				}
				const char * col = k < left[t] ? "\x1b[38;5;153m" : "\x1b[38;5;88m";
				for (int c = 0; c < st.shape.w; c++) {
					row += st.shape.at(r, c) ? std::string(col) + "\u2588\u2588\x1b[0m" : std::string("  ");
					w += 2;
				}
			}
			add(row, w);
		}
	}
	width = 0;
	for (int w : vis) width = std::max(width, w);
	return lines;
}

static int term_cols() {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
	return 80;
}

struct Look {
	int bg, fg;
	bool bold;
	std::string text;  // 3 characters
};

static Look cell_look(const App & a, int i, bool fresh, bool stale_numbers, double maxp) {
	char ch = a.board.cells[i];
	if (ch == 'o') return {17, 75, false, " o "};
	if (ch == 'x') return {160, 231, true, " X "};
	if (ch == 'S') return {52, 210, false, " # "};
	if (!fresh && !stale_numbers) return {235, 242, false, " . "};
	u128 cnt = a.last.r.cell_counts[i];
	double pr = (double)cnt / (double)a.last.r.total;
	char buf[8];
	if (cnt == 0) snprintf(buf, sizeof buf, " . ");
	else if (pr < 0.005) snprintf(buf, sizeof buf, " <1");
	else if (cnt != a.last.r.total && pr >= 0.995) snprintf(buf, sizeof buf, ">99");  // "100" means certain
	else snprintf(buf, sizeof buf, "%3.0f", 100 * pr);
	if (!fresh) return {235, 242, false, buf};  // out of date: greyed out
	if (cnt == 0) return {234, 240, false, buf};
	int k = maxp > 0 ? (int)std::lround(pr / maxp * (NRAMP - 1)) : 0;
	return {RAMP[k], k >= NRAMP / 2 ? 16 : 252, false, buf};
}

static std::string sgr(int bg, int fg, bool bold) {
	std::string s = "\x1b[";
	if (bg >= 0) s += "48;5;" + std::to_string(bg) + ";";
	s += "38;5;" + std::to_string(fg);
	if (bold) s += ";1";
	return s + "m";
}

// Column header + board rows, each line ending in "\x1b[K\n".
//  compact: one line per row, cells 5 columns wide (a wide rectangle)
//  square:  cells 3 columns wide and 1.5 lines tall - a full line plus the
//           top half of the next one ("\u2580") - which is close to square
//           since terminal characters are about twice as tall as wide.
static std::string render_board(const App & a, bool square, bool fresh, bool stale_numbers, int best) {
	const Board & b = a.board;
	double maxp = 0;
	if (fresh)
		for (int i = 0; i < b.W * b.H; i++)
			if (b.cells[i] == '.') maxp = std::max(maxp, (double)a.last.r.cell_counts[i] / (double)a.last.r.total);
	const std::string EOL = "\x1b[K\n";
	std::string s = "     ";
	for (int c = 0; c < b.W; c++) {
		std::string n = col_name(c);
		s += square ? " " + (n.size() == 1 ? " " + n + " " : n + " ") : "  " + n + std::string(3 - n.size(), ' ');
	}
	s += EOL;
	for (int r = 0; r < b.H; r++) {
		char lbl[8];
		snprintf(lbl, sizeof lbl, " %3d ", r + 1);
		if (!square) {
			s += lbl;
			for (int c = 0; c < b.W; c++) {
				int i = r * b.W + c;
				Look l = cell_look(a, i, fresh, stale_numbers, maxp);
				bool cur = (r == a.cr && c == a.cc);
				std::string style = sgr(l.bg, l.fg, l.bold) + (cur ? "\x1b[7m" : "");
				s += style + (cur ? "[" : (i == best ? "*" : " ")) + l.text + (cur ? "]" : " ") + "\x1b[0m";
			}
			s += EOL;
			continue;
		}
		// gaps[c] is the column left of cell c (and right of cell c-1)
		std::vector<std::string> gaps(b.W + 1, " ");
		if (best >= 0 && best / b.W == r) gaps[best % b.W] = "\x1b[1;38;5;226m*\x1b[0m";
		std::vector<std::string> gaps2(b.W + 1, " ");
		if (r == a.cr) {
			gaps[a.cc] = gaps2[a.cc] = "\x1b[1;38;5;231m\u2590\x1b[0m";      // right half block
			gaps[a.cc + 1] = gaps2[a.cc + 1] = "\x1b[1;38;5;231m\u258c\x1b[0m";  // left half block
			// best shot just right of the cursor: its '*' slot holds the cursor bar,
			// so mark it on the lower line, right of the cell
			if (best >= 0 && best / b.W == r && best % b.W == a.cc + 1)
				gaps2[a.cc + 2] = "\x1b[1;38;5;226m*\x1b[0m";
		}
		std::string top = lbl, bot = "     ";
		for (int c = 0; c < b.W; c++) {
			int i = r * b.W + c;
			Look l = cell_look(a, i, fresh, stale_numbers, maxp);
			top += gaps[c] + sgr(l.bg, l.fg, l.bold) + l.text + "\x1b[0m";
			bot += gaps2[c] + sgr(-1, l.bg, false) + "\u2580\u2580\u2580\x1b[0m";
		}
		s += top + gaps[b.W] + EOL + bot + gaps2[b.W] + EOL;
	}
	return s;
}

static int term_rows() {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
	return 24;
}

static std::string build_frame(const App & a, bool square);
static int g_spin = 0;

// Terminal rows a frame occupies, counting lines that wrap.
static int frame_rows(const std::string & f, int cols) {
	int rows = 0, w = 0;
	for (size_t i = 0; i < f.size(); i++) {
		unsigned char ch = f[i];
		if (ch == 27) {  // skip CSI sequence
			while (++i < f.size() && !((f[i] >= 'A' && f[i] <= 'Z') || (f[i] >= 'a' && f[i] <= 'z'))) {
			}
		} else if (ch == '\n') {
			rows += w > 0 ? (w + cols - 1) / cols : 1;
			w = 0;
		} else if ((ch & 0xC0) != 0x80) {
			w++;  // one column per UTF-8 character
		}
	}
	return rows;
}

// Square cells when the user wants them and they fit; never more lines than
// the terminal has, or it would scroll and the next redraw would be garbled.
static void draw(const App & a) {
	int rows = term_rows(), cols = term_cols();
	g_spin++;
	auto lines = [cols](const std::string & f) { return frame_rows(f, cols); };
	std::string f = build_frame(a, a.square);
	if (a.square && lines(f) > rows - 1) f = build_frame(a, false);
	while (lines(f) > rows - 1 && f.size() > 1) {
		size_t e = f.rfind('\n', f.size() - 2);  // drop the last line
		f.resize(e == std::string::npos ? 0 : e + 1);
	}
	term_write("\x1b[H" + f + "\x1b[J");
}

static std::string build_frame(const App & a, bool square) {
	static const char * SPIN = "|/-\\";
	const Board & b = a.board;
	bool fresh = a.fresh() && a.last.ok;
	bool stale_numbers = a.have_last && a.last.ok && !fresh;
	int best = a.best_cell();

	std::string s;
	s += "\x1b[1m Battleships\x1b[0m   " + std::to_string(b.W) + "x" + std::to_string(b.H) + " board\x1b[K\n";
	s += "\x1b[K\n";

	s += render_board(a, square, fresh, stale_numbers, best);
	s += "\x1b[K\n";

	// status
	std::string status;
	int ci = a.cr * b.W + a.cc;
	status = " Cursor " + col_name(a.cc) + std::to_string(a.cr + 1);
	if (fresh && b.cells[ci] == '.') {
		char buf[32];
		snprintf(buf, sizeof buf, " (%.2f%%)", 100.0 * (double)a.last.r.cell_counts[ci] / (double)a.last.r.total);
		status += buf;
	}
	s += status + "\x1b[K\n";

	if (!a.shape_error.empty()) {
		s += " \x1b[1;31m" + a.shape_error + "\x1b[0m\x1b[K\n\x1b[K\n";
	} else if (a.computing() && !fresh) {
		s += std::string(" computing ") + SPIN[g_spin % 4] + "\x1b[K\n\x1b[K\n";
	} else if (a.have_last && !a.last.ok && a.last.gen == a.gen) {
		s += " \x1b[1;31m" + a.last.error + "\x1b[0m\x1b[K\n\x1b[K\n";
	} else if (fresh) {
		char buf[160];
		snprintf(buf, sizeof buf, " %s configurations   (%.2fs)", to_string(a.last.r.total).c_str(), a.last.secs);
		s += std::string(buf) + "\x1b[K\n";
		if (best >= 0) {
			snprintf(buf, sizeof buf, " Best shot: \x1b[1m%s%d\x1b[0m  (%.2f%%)  - marked *, press b to jump",
			         col_name(best % b.W).c_str(), best / b.W + 1,
			         100.0 * (double)a.last.r.cell_counts[best] / (double)a.last.r.total);
			s += buf;
		} else {
			s += " No unknown cell can hold a ship - every ship is found.";
		}
		s += "\x1b[K\n";
	} else {
		s += "\x1b[K\n\x1b[K\n";
	}
	s += " " + a.message + "\x1b[K\n\x1b[K\n";
	s += " \x1b[2marrows/hjkl move   x hit   o miss   s sunk (toggle hit group)   space clear\x1b[0m\x1b[K\n";
	s += " \x1b[2mb/enter best shot   u undo   r reset   v view   w save   q quit\x1b[0m\x1b[K\n";
	// put the fleet panel to the right of the board if it fits, else below
	int pw = 0;
	std::vector<std::string> panel = fleet_panel(a, pw);
	int col = 5 + (square ? 4 * b.W + 1 : 5 * b.W) + 2;  // past the board rows
	const std::string EOL = "\x1b[K\n";
	if (term_cols() >= col + pw - 1) {
		// beside the title, column header and board rows; the rest of the
		// panel gets its own lines, pushing the status down
		size_t board_lines = 3 + (size_t)b.H * (square ? 2 : 1);
		std::string merged, at = "\x1b[" + std::to_string(col) + "G";
		size_t pos = 0;
		for (size_t i = 0;; i++) {
			if (i == board_lines)
				for (size_t j = board_lines; j < panel.size(); j++) merged += at + panel[j] + EOL;
			size_t e = s.find(EOL, pos);
			if (e == std::string::npos) {
				merged += s.substr(pos);
				break;
			}
			merged += s.substr(pos, e - pos);
			if (i < board_lines && i < panel.size()) merged += at + panel[i];
			merged += EOL;
			pos = e + EOL.size();
		}
		s = merged;
	} else {
		s += EOL;
		for (auto & l : panel) s += " " + l + EOL;
	}
	return s;
}

// ---------------------------------------------------------------- edits

static int cur_index(const App & a) { return a.cr * a.board.W + a.cc; }

// Changing one cell of a sunk ship turns the rest of it back into hits, so the
// remaining S cells never form a half ship.
static void unsink_at(App & a, int i) {
	if (a.board.cells[i] != 'S') return;
	for (int x : component(a.board, i)) a.board.cells[x] = 'x';
}

static void set_cell(App & a, char ch) {
	int i = cur_index(a);
	char before = a.board.cells[i];
	unsink_at(a, i);
	a.board.cells[i] = (before == ch) ? '.' : ch;  // pressing again clears
}

static void toggle_sunk(App & a) {
	int i = cur_index(a);
	char ch = a.board.cells[i];
	if (ch != 'x' && ch != 'S') {
		a.message = "s works on a hit: mark all cells of the ship as hits (x), then press s on one of them";
		return;
	}
	char to = ch == 'x' ? 'S' : 'x';
	for (int x : component(a.board, i)) a.board.cells[x] = to;
}

static bool save(App & a) {
	if (!a.file_ours && std::ifstream(a.file)) {
		a.message = a.file + " already exists; run ./bin/tui " + a.file + " to continue it";
		return false;
	}
	std::ofstream out(a.file);
	out << board_to_text(a.board);
	out.close();
	a.message = out ? "saved to " + a.file : "cannot write " + a.file;
	if (out) a.file_ours = true;
	return (bool)out;
}

int main(int argc, char ** argv) {
	App a;
	std::string fleet_name = "game";
	int W = 10, H = 10;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--fleet") && i + 1 < argc) fleet_name = argv[++i];
		else if (!strcmp(argv[i], "--no-rotate")) a.rotate = false;
		else if (!strcmp(argv[i], "--no-sunk-announce")) a.announced = false;
		else if (!strcmp(argv[i], "--threads") && i + 1 < argc) a.opt.threads = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
			if (sscanf(argv[++i], "%dx%d", &W, &H) != 2 || W < 1 || H < 1 || W * H > 128) {
				fprintf(stderr, "error: --size must be WxH with at most 128 cells\n");
				return 1;
			}
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			printf("usage: tui [--fleet game|czech|legacy] [--size WxH] [--no-rotate] [--no-sunk-announce] [--threads N] [state-file]\n");
			return 0;
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "usage: tui [--fleet game|czech|legacy] [--size WxH] [--no-rotate] [--no-sunk-announce] [--threads N] [state-file]\n");
			return 1;
		} else a.file = argv[i];
	}
	try {
		a.fleet = fleet_by_name(fleet_name);
		a.board = Board(W, H);
		if (!a.file.empty()) {
			a.file_ours = true;
			std::ifstream in(a.file);
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			// a missing or blank file starts an empty board and is written on save
			if (text.find_first_not_of(" \t\r\n") != std::string::npos) {
				std::istringstream ss(text);
				a.board = read_board(ss);
			}
		} else {
			a.file = "state.txt";
		}
	} catch (const std::exception & e) {
		fprintf(stderr, "error: %s\n", e.what());
		return 1;
	}
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
		fprintf(stderr, "error: tui needs a terminal; use bin/play for files and pipes\n");
		return 1;
	}

	term_raw();
	a.changed();
	bool dirty = true;
	for (;;) {
		if (a.poll_job()) dirty = true;
		if (dirty || a.computing()) draw(a);
		dirty = false;
		int k = read_key(a.computing() ? 100 : 500);
		if (k == K_NONE) continue;
		dirty = true;
		Board & b = a.board;
		switch (k) {
			case K_UP: case 'k': a.cr = (a.cr + b.H - 1) % b.H; break;
			case K_DOWN: case 'j': a.cr = (a.cr + 1) % b.H; break;
			case K_LEFT: case 'h': a.cc = (a.cc + b.W - 1) % b.W; break;
			case K_RIGHT: case 'l': a.cc = (a.cc + 1) % b.W; break;
			case 'x': case 'X': a.edit([](App & a) { set_cell(a, 'x'); }); break;
			case 'o': case 'O': case 'm': a.edit([](App & a) { set_cell(a, 'o'); }); break;
			case 's': case 'S': a.edit(toggle_sunk); break;
			case ' ': case '.': case 127: case 8:
				a.edit([](App & a) {
					int i = cur_index(a);
					unsink_at(a, i);
					a.board.cells[i] = '.';
				});
				break;
			case 'b': case 'B': case '\r': case '\n': {
				int best = a.best_cell();
				if (best >= 0) {
					a.cr = best / b.W;
					a.cc = best % b.W;
				}
				break;
			}
			case 'u': case 'U':
				if (a.undo.empty()) a.message = "nothing to undo";
				else {
					a.board = a.undo.back();
					a.undo.pop_back();
					a.message.clear();
					a.changed();
				}
				break;
			case 'r': case 'R':
				a.edit([](App & a) { a.board = Board(a.board.W, a.board.H); });
				a.message = "board reset (u to undo)";
				break;
			case 'v': case 'V': a.square = !a.square; break;
			case 'w': case 'W': save(a); break;
			case 'q': case 'Q': case 3:
				term_restore();
				fflush(stdout);
				_exit(0);  // don't wait for a running computation
		}
	}
}
