#include <boost/algorithm/string.hpp>
#include "Core/Config.hpp"
#include "Core/Node.hpp"
#include "common/CommandLine.hpp"
#include "common/ConsoleTools.hpp"
#include "logging/LoggerManager.hpp"
#include "platform/ExclusiveLock.hpp"
#include "platform/Network.hpp"
#include "platform/PathTools.hpp"
#include "version.hpp"

using namespace cryptonote;

static const char USAGE[] =
    R"(cryptonoted )" cryptonote_VERSION_STRING R"(.

Usage:
  cryptonoted [options]
  cryptonoted --help | -h
  cryptonoted --version | -v

Options:
  --export-blocks=<directory>          Export blockchain into specified directory as blocks.bin and blockindexes.bin, then exit. This overwrites existing files.
  --allow-local-ip                     Allow local ip add to peer list, mostly in debug purposes.
  --testnet                            Configure for testnet.
  --p2p-bind-address=<ip:port>         Interface and port for P2P network protocol [default: 0.0.0.0:8080].
  --p2p-external-port=<port>           External port for P2P network protocol, if port forwarding used with NAT [default: 8080].
  --cryptonoted-bind-address=<ip:port>   Interface and port for cryptonoted RPC [default: 0.0.0.0:8081].
  --seed-node-address=<ip:port>        Specify list (one or more) of nodes to start connecting to.
  --priority-node-address=<ip:port>    Specify list (one or more) of nodes to connect to and attempt to keep the connection open.
  --exclusive-node-address=<ip:port>   Specify list (one or more) of nodes to connect to only. All other nodes including seed nodes will be ignored.
  --data-folder=<full-path>            Folder for blockchain, logs and peer DB [default: )" platform_DEFAULT_DATA_FOLDER_PATH_PREFIX
    R"(cryptonote].
)"
#if platform_USE_SSL
    R"(  --ssl-certificate-pem-file=<file>    Full path to file containing both server SSL certificate and private key in PEM format.
  --ssl-certificate-password=<pass>    DEPRECATED. Will read password from stdin if not specified.)"
#endif
    R"(  --cryptonoted-authorization=<usr:pass> HTTP authorization for RPC.)";

int main(int argc, const char *argv[]) try {
	common::console::UnicodeConsoleSetup console_setup;
	auto idea_start = std::chrono::high_resolution_clock::now();
	common::CommandLine cmd(argc, argv);

	std::string export_blocks;
	if (const char *pa = cmd.get("--export-blocks"))
		export_blocks = pa;
	cryptonote::Config config(cmd);
	cryptonote::Currency currency(config.is_testnet);

	if (cmd.should_quit(USAGE, cryptonote::app_version()))
		return 0;

	if (!config.ssl_certificate_pem_file.empty() && !config.ssl_certificate_password) {
		std::string ssl_certificate_password;
		std::cout << "Enter ssl certificate password: " << std::flush;
		std::getline(std::cin, ssl_certificate_password);
		boost::algorithm::trim(ssl_certificate_password);

		config.ssl_certificate_password = ssl_certificate_password;
	}

	const std::string coinFolder = config.get_data_folder();

	platform::ExclusiveLock coin_lock(coinFolder, "cryptonoted.lock");

	logging::LoggerManager logManager;
	logManager.configure_default(config.get_data_folder("logs"), "cryptonoted-");

	BlockChainState block_chain(logManager, config, currency);

	if (!export_blocks.empty()) {
		if (!LegacyBlockChainWriter::export_blockchain2(export_blocks, block_chain))
			return 1;
		return 0;
	}

	boost::asio::io_service io;
	platform::EventLoop run_loop(io);

	Node node(logManager, config, block_chain);

	auto idea_ms =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - idea_start);
	std::cout << "cryptonoted started seconds=" << double(idea_ms.count()) / 1000 << std::endl;
	while (!io.stopped()) {
		if (node.on_idle())  // Using it to load blockchain
			io.poll();
		else
			io.run_one();
	}
	return 0;
} catch (const platform::ExclusiveLock::FailedToLock &ex) {
	std::cout << "cryptonoted already running - " << ex.what() << std::endl;
	return api::CRYPTONOTED_ALREADY_RUNNING;
} catch (const std::exception &ex) {  // On Windows what() is not printed if thrown from main
	std::cout << "Exception in main() - " << ex.what() << std::endl;
	throw;
}
