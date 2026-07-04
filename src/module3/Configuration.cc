#include "Configuration.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace searchengine {

using std::ifstream;
using std::stringstream;
using std::getline;
using std::cout;
using std::cerr;
using std::endl;

Configuration& Configuration::getInstance(const string &filepath)
{
    static Configuration instance(filepath);
    return instance;
}

unordered_map<string, string>& Configuration::getConfigMap()
{
    return _configMap;
}

Configuration::Configuration(const string &filepath)
: _filepath(filepath)
{
    loadConf();
}

void Configuration::loadConf()
{
    ifstream ifs(_filepath);

    if(!ifs) {
        cerr << "Configuration file open error: " << _filepath << endl;
        exit(1);
    }

    string line;
    string key, value;

    while(getline(ifs, line)) {
        stringstream ss(line);
        if (!(ss >> key >> value) || key[0] == '#') {
            continue;
        }
        _configMap[key] = value;
    }

    cout << "[Config] loaded " << _configMap.size()
         << " entries from " << _filepath << endl;
}

} // namespace searchengine
