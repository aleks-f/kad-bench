#include <cstdint>
#include <cstdlib>

#include <future>
#include <iostream>
#include <chrono>
#include <sstream>
#include <random>

#include <kademlia/endpoint.hpp>
#include <kademlia/error.hpp>
#include <kademlia/first_session.hpp>
#include <kademlia/session.hpp>

namespace k = kademlia;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using std::chrono::steady_clock;
using std::chrono::steady_clock;

namespace {

int _saved = 0, _loaded = 0;
int _savedBytes = 0, _loadedBytes = 0;
int _saveTime = 0, _loadTime = 0;

int random_session(int max_id)
{
    std::random_device rd;
    std::default_random_engine eng(rd());
    std::uniform_int_distribution<int> distr(0, max_id);
    return distr(eng);
}

void
load( k::session & session
    , std::string const& key )
{
	steady_clock::time_point begin = steady_clock::now();
    auto on_load = [ key, begin ] ( std::error_code const& error
                           , k::session::data_type const& data )
    {
		steady_clock::time_point end = steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
        if ( error )
            std::cerr << "Failed to load \"" << key << "\"" << std::endl;
        else
        {
        	_loadTime += elapsed;
			++_loaded;
			_loadedBytes += data.size();
            std::string const& str{ data.begin(), data.end() };
            //std::cout << "Loaded \"" << key << "\" as \""
            //          << str << "\"" << std::endl;
        }
    };

    session.async_load( key, std::move( on_load ) );
}

void
save( k::session & session
    , std::string const& key
    , std::string const& value )
{
	steady_clock::time_point begin = steady_clock::now();
	std::size_t sz = value.size();
    auto on_save = [ key, begin, sz ] ( std::error_code const& error )
    {
    	steady_clock::time_point end = steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
        if ( error )
            std::cerr << "Failed to save \"" << key << "\"" << std::endl;
        else
		{
			_saveTime += elapsed;
			++_saved;
			_savedBytes += sz;
			//std::cout << "Saved \"" << key << "\"" << std::endl;
		}
    };

    session.async_save( key, value, std::move( on_save ) );
}

}

int main
        ( int argc
        , char** argv )
{
    // Parse command line arguments
    std::uint16_t const port = 1234;

    // Create the session
    k::first_session bootstrap_session{ k::endpoint{ "0.0.0.0", port }
                            , k::endpoint{ "::", port } };
    // Start the main loop thread
    auto bootstrap_loop = std::async( std::launch::async
                               , &k::first_session::run, &bootstrap_session );

    int peers = 3, chunks = 24, chunkSize = 50000;
    uint16_t sessPort = port + 1;
    std::vector<k::session*> sessions;
    std::vector<std::future<std::error_code>> peer_loops;
	for (int i = 0; i < peers; ++i)
	{
		k::session* p_session = new k::session{ k::endpoint{ "127.0.0.1", port }
					  , k::endpoint{ "127.0.0.1", sessPort }
					  , k::endpoint{ "::1", sessPort} };
		peer_loops.emplace_back(std::async( std::launch::async
                               , &k::session::run, p_session ));
        sessions.push_back(p_session);
		std::cout << "peer session connected to 127.0.0.1:" << port <<
			", listening on 127.0.0.1:" << sessPort << ", " <<
			"[::1]:" << sessPort << std::endl;
		++sessPort;
	}

	sleep_for(milliseconds(100));
	for (int i = 0; i < chunks; ++i)
	{
		std::string k("k"), v;
		k += std::to_string(i);
		v += std::to_string(i);
		v.resize(chunkSize);
		save(*sessions[random_session(peers-1)], k, v);
		while (_saved < i + 1) sleep_for(milliseconds(1));
	}

	for (int i = 0; i < chunks; ++i)
	{
		std::string k("k");
		k += std::to_string(i);
		load(*sessions[random_session(peers-1)], k);
		while (_loaded < i + 1) sleep_for(milliseconds(1));
	}


		std::cout << "Summary\n=======\n" << peers << " peers, " <<
							chunks << " chunks of " << chunkSize << " bytes\n"
							"saved " << _savedBytes << " bytes, loaded " << _loadedBytes << " bytes\n"
							"Total save time: " << float(_saveTime)/1000 << " [ms]\n"
							"Total load time: " << float(_loadTime)/1000 << " [ms]" << std::endl;

    // Stop the main loop thread
    bootstrap_session.abort();
    // Wait for the main loop thread termination
    auto failure = bootstrap_loop.get();
    if ( failure != k::RUN_ABORTED )
	{
		std::cerr << failure.message() << std::endl;
	}
	for (int i = 0; i < peers; ++i)
	{
		sessions[i]->abort();
		auto failure = peer_loops[i].get();
		if ( failure != k::RUN_ABORTED )
			std::cerr << failure.message() << std::endl;
		delete sessions[i];
	}
}
