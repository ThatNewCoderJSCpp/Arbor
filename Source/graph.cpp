#include "arbor/graph.hpp"

#include <grove/bark/parser.hpp>
#include <grove/bark/stylesheet.hpp>
#include <grove/bark/value.hpp>
#include <grove/common/diagnostics.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>
#include <sstream>

namespace arbor {

std::string Item::field(const std::string& name) const {
    auto it = fields.find(name);
    if (it != fields.end()) return it->second;
    if (const KindInfo* k = find_kind(kind)) {
        for (const FieldSpec& f : k->fields) {
            if (f.id == name) return f.value;
        }
    }
    return std::string();
}

bool Item::option(const std::string& name) const {
    auto it = options.find(name);
    if (it != options.end()) return it->second;
    if (const KindInfo* k = find_kind(kind)) {
        for (const OptionSpec& o : k->options) {
            if (o.id == name) return o.value;
        }
    }
    return false;
}

Item* Graph::find_item(const std::string& id) {
    for (Item& i : items) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

const Item* Graph::find_item(const std::string& id) const {
    for (const Item& i : items) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

Slider* Graph::find_slider(const std::string& name) {
    for (Slider& s : sliders) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

Item& Graph::add_item(const std::string& kind) {
    Item it;
    it.id = "item-" + std::to_string(next_item++);
    it.kind = kind;
    std::size_t used = 0;
    for (const Item& other : items) {
        if (other.kind != "define") ++used;
    }
    it.color = palette()[used % palette().size()];
    if (const KindInfo* k = find_kind(kind)) {
        for (const FieldSpec& f : k->fields) it.fields[f.id] = f.value;
        for (const OptionSpec& o : k->options) it.options[o.id] = o.value;
    }
    items.push_back(it);
    return items.back();
}

bool Graph::remove_item(const std::string& id) {
    auto it = std::find_if(items.begin(), items.end(), [&](const Item& i) { return i.id == id; });
    if (it == items.end()) return false;
    items.erase(it);
    return true;
}

bool Graph::move_item(const std::string& id, int delta) {
    auto it = std::find_if(items.begin(), items.end(), [&](const Item& i) { return i.id == id; });
    if (it == items.end()) return false;
    const long long from = it - items.begin();
    const long long to = std::clamp<long long>(from + delta, 0, static_cast<long long>(items.size()) - 1);
    if (from == to) return false;
    Item moving = *it;
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, moving);
    return true;
}

std::string make_id() {
    static std::mt19937_64 rng(static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    char tail[16];
    std::snprintf(tail, sizeof tail, "%04x", static_cast<unsigned>(rng() & 0xffffu));
    return std::string("g") + stamp + "-" + tail;
}

std::string now_text() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &tm);
    return stamp;
}

double slider_step(const Slider& s) {
    if (s.step > 0.0) return s.step;
    const double span = std::fabs(s.max - s.min);
    if (span <= 0.0) return 0.01;
    return std::pow(10.0, std::floor(std::log10(span)) - 2.0);
}

Graph new_graph(const std::string& first_kind) {
    Graph g;
    g.id = make_id();
    const KindInfo* k = find_kind(first_kind);
    g.space = k ? k->space : Space::Plane;
    g.title = k ? k->title : std::string("Untitled");
    g.created = now_text();
    g.modified = g.created;
    if (k) g.add_item(first_kind);
    if (k && (k->id == "domain" || k->id == "conformal")) g.view2.scale = 90.0;
    if (k && k->id == "complex_surface") {
        g.view3.range = 2.5;
        g.view3.zrange = 3.0;
    }
    if (k && k->id == "riemann") {
        g.view3.range = 2.5;
        g.view3.zrange = 2.0;
    }
    if (k && k->id == "complex_curve") {
        g.view3.range = 7.0;
        g.view3.target = Vec3{ 6.3, 0.0, 0.0 };
        g.view3.auto_z = false;
        g.view3.zrange = 3.0;
    }
    if (k && (k->id == "transform2d")) g.view2.scale = 70.0;
    if (k && k->id == "sequence") {
        g.view2.equal_axes = false;
        g.view2.cx = 56.0;
        g.view2.cy = 4300.0;
        g.view2.scale = 6.0;
        g.view2.scale_y = 0.058;
    }
    if (k && k->id == "escape") {
        g.view2.cx = -0.6;
        g.view2.scale = 200.0;
    }
    if (k && k->id == "turmite") {
        g.view2.cx = -0.6;
        g.view2.scale = 45.0;
    }
    if (k && k->id == "pde") {
        g.view3.target = Vec3{ 0.0, 2.0, 0.0 };
        g.view3.range = 3.6;
        g.view3.range_y = 2.2;
    }
    if (k && k->id == "pde_steady") {
        g.view3.target = Vec3{ 0.5, 0.5, 0.5 };
        g.view3.range = 0.65;
        g.view3.auto_z = false;
        g.view3.zrange = 0.65;
    }
    if (k && k->id == "pde_march") {
        g.view3.target = Vec3{ 2.0, 0.0, 0.0 };
        g.view3.range = 2.2;
        g.view3.range_y = 3.4;
    }
    return g;
}

namespace {

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

std::string num(double v) {
    std::ostringstream os;
    os.precision(15);
    os << v;
    return os.str();
}

std::string value_of(const grove::bark::ValueList& values) {
    if (values.empty()) return std::string();
    const grove::bark::Value& v = values.front();
    if (v.is_string() || v.is_ident()) return v.text;
    if (v.is_color()) return color_hex(Rgba{ v.color.r, v.color.g, v.color.b, v.color.a });
    return grove::bark::to_string(values);
}

double number_of(const grove::bark::ValueList& values, double fallback) {
    if (values.empty()) return fallback;
    if (values.front().is_number()) return values.front().number;
    try {
        return std::stod(value_of(values));
    } catch (...) {
        return fallback;
    }
}

bool bool_of(const grove::bark::ValueList& values, bool fallback) {
    const std::string t = value_of(values);
    if (t == "true" || t == "yes" || t == "on" || t == "1") return true;
    if (t == "false" || t == "no" || t == "off" || t == "0") return false;
    return fallback;
}

} // namespace

std::string save_graph(const Graph& g) {
    std::ostringstream os;
    os << "graph {\n";
    os << "    id: " << quote(g.id) << ";\n";
    os << "    title: " << quote(g.title) << ";\n";
    os << "    space: " << quote(space_id(g.space)) << ";\n";
    os << "    created: " << quote(g.created) << ";\n";
    os << "    modified: " << quote(g.modified) << ";\n";
    os << "    grid: " << (g.show_grid ? "true" : "false") << ";\n";
    os << "    axes: " << (g.show_axes ? "true" : "false") << ";\n";
    os << "    labels: " << (g.show_labels ? "true" : "false") << ";\n";
    os << "    degrees: " << (g.degrees ? "true" : "false") << ";\n";
    os << "    next-item: " << g.next_item << ";\n";
    os << "}\n\n";
    os << "view {\n";
    os << "    center-x: " << num(g.view2.cx) << ";\n";
    os << "    center-y: " << num(g.view2.cy) << ";\n";
    os << "    scale: " << num(g.view2.scale) << ";\n";
    os << "    scale-y: " << num(g.view2.scale_y) << ";\n";
    os << "    equal-axes: " << (g.view2.equal_axes ? "true" : "false") << ";\n";
    if (!g.view2.bound_xmin.empty()) os << "    bound-x-min: " << quote(g.view2.bound_xmin) << ";\n";
    if (!g.view2.bound_xmax.empty()) os << "    bound-x-max: " << quote(g.view2.bound_xmax) << ";\n";
    if (!g.view2.bound_ymin.empty()) os << "    bound-y-min: " << quote(g.view2.bound_ymin) << ";\n";
    if (!g.view2.bound_ymax.empty()) os << "    bound-y-max: " << quote(g.view2.bound_ymax) << ";\n";
    os << "    yaw: " << num(g.view3.yaw) << ";\n";
    os << "    pitch: " << num(g.view3.pitch) << ";\n";
    os << "    zoom: " << num(g.view3.zoom) << ";\n";
    os << "    target-x: " << num(g.view3.target.x) << ";\n";
    os << "    target-y: " << num(g.view3.target.y) << ";\n";
    os << "    target-z: " << num(g.view3.target.z) << ";\n";
    os << "    range: " << num(g.view3.range) << ";\n";
    if (g.view3.range_y > 0.0) os << "    range-y: " << num(g.view3.range_y) << ";\n";
    os << "    z-range: " << num(g.view3.zrange) << ";\n";
    os << "    auto-z: " << (g.view3.auto_z ? "true" : "false") << ";\n";
    os << "    perspective: " << (g.view3.perspective ? "true" : "false") << ";\n";
    os << "}\n";
    for (const Item& it : g.items) {
        os << "\n" << it.id << " {\n";
        os << "    kind: " << quote(it.kind) << ";\n";
        os << "    color: " << quote(it.color) << ";\n";
        os << "    visible: " << (it.visible ? "true" : "false") << ";\n";
        os << "    expanded: " << (it.expanded ? "true" : "false") << ";\n";
        for (const auto& [k, v] : it.fields) os << "    field-" << k << ": " << quote(v) << ";\n";
        for (const auto& [k, v] : it.options) os << "    option-" << k << ": " << (v ? "true" : "false") << ";\n";
        os << "}\n";
    }
    for (const Slider& s : g.sliders) {
        os << "\nslider {\n";
        os << "    name: " << quote(s.name) << ";\n";
        os << "    value: " << num(s.value) << ";\n";
        os << "    min: " << num(s.min) << ";\n";
        os << "    max: " << num(s.max) << ";\n";
        os << "    step: " << num(s.step) << ";\n";
        os << "}\n";
    }
    return os.str();
}

bool load_graph(const std::string& text, Graph& out, std::string& error) {
    grove::Diagnostics diags;
    const grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(text, "graph.arbor", diags);
    Graph g;
    bool saw_graph = false;
    for (const grove::bark::Rule& rule : sheet.rules()) {
        for (const grove::bark::Selector& sel : rule.selectors) {
            const std::string name = sel.to_string();
            std::map<std::string, const grove::bark::ValueList*> d;
            for (const grove::bark::Declaration& decl : rule.declarations) d[decl.property] = &decl.values;
            auto get = [&](const char* key) -> std::string { auto it = d.find(key); return it == d.end() ? std::string() : value_of(*it->second); };
            auto getn = [&](const char* key, double fb) { auto it = d.find(key); return it == d.end() ? fb : number_of(*it->second, fb); };
            auto getb = [&](const char* key, bool fb) { auto it = d.find(key); return it == d.end() ? fb : bool_of(*it->second, fb); };
            if (name == "graph") {
                saw_graph = true;
                g.id = get("id");
                g.title = get("title");
                g.space = space_from_id(get("space"));
                g.created = get("created");
                g.modified = get("modified");
                g.show_grid = getb("grid", true);
                g.show_axes = getb("axes", true);
                g.show_labels = getb("labels", true);
                g.degrees = getb("degrees", false);
                g.next_item = static_cast<int>(getn("next-item", 1));
            } else if (name == "view") {
                g.view2.cx = getn("center-x", 0.0);
                g.view2.cy = getn("center-y", 0.0);
                g.view2.scale = getn("scale", 40.0);
                g.view2.scale_y = getn("scale-y", g.view2.scale);
                g.view2.equal_axes = getb("equal-axes", true);
                g.view2.bound_xmin = get("bound-x-min");
                g.view2.bound_xmax = get("bound-x-max");
                g.view2.bound_ymin = get("bound-y-min");
                g.view2.bound_ymax = get("bound-y-max");
                g.view3.yaw = getn("yaw", -0.7);
                g.view3.pitch = getn("pitch", 0.45);
                g.view3.zoom = getn("zoom", 1.0);
                g.view3.target = Vec3{ getn("target-x", 0.0), getn("target-y", 0.0), getn("target-z", 0.0) };
                g.view3.range = getn("range", 5.0);
                g.view3.range_y = getn("range-y", 0.0);
                g.view3.zrange = getn("z-range", 5.0);
                g.view3.auto_z = getb("auto-z", true);
                g.view3.perspective = getb("perspective", true);
            } else if (name == "slider") {
                Slider s;
                s.name = get("name");
                s.value = getn("value", 1.0);
                s.min = getn("min", -10.0);
                s.max = getn("max", 10.0);
                s.step = getn("step", 0.0);
                if (!s.name.empty()) g.sliders.push_back(s);
            } else if (name.rfind("item-", 0) == 0) {
                Item it;
                it.id = name;
                it.kind = get("kind");
                it.color = get("color");
                it.visible = getb("visible", true);
                it.expanded = getb("expanded", true);
                for (const auto& [key, values] : d) {
                    if (key.rfind("field-", 0) == 0) it.fields[key.substr(6)] = value_of(*values);
                    else if (key.rfind("option-", 0) == 0) it.options[key.substr(7)] = bool_of(*values, false);
                }
                if (find_kind(it.kind)) g.items.push_back(it);
                const std::string digits = name.substr(5);
                try {
                    g.next_item = std::max(g.next_item, std::stoi(digits) + 1);
                } catch (...) {
                }
            }
        }
    }
    if (!saw_graph) {
        error = "this isn't an Arbor graph";
        return false;
    }
    if (g.id.empty()) g.id = make_id();
    if (g.title.empty()) g.title = "Untitled";
    out = std::move(g);
    return true;
}

} // namespace arbor
