// Copyright (c) 2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

//////////////////////////
#include <rpc/RpcServer.h>
//////////////////////////

#include <iostream>

#include "version.h"

#include <errors/ValidateParameters.h>
#include <logger/Logger.h>
#include <utilities/ColouredMsg.h>
#include <utilities/FormatTools.h>

RpcServer::RpcServer(
    const uint16_t bindPort,
    const std::string rpcBindIp,
    const std::string corsHeader,
    const std::string feeAddress,
    const uint64_t feeAmount,
    const RpcMode rpcMode,
    const std::shared_ptr<CryptoNote::Core> core,
    const std::shared_ptr<CryptoNote::NodeServer> p2p,
    const std::shared_ptr<CryptoNote::ICryptoNoteProtocolHandler> syncManager):
    m_port(bindPort),
    m_host(rpcBindIp),
    m_corsHeader(corsHeader),
    m_feeAddress(feeAddress),
    m_feeAmount(feeAmount),
    m_rpcMode(rpcMode),
    m_core(core),
    m_p2p(p2p),
    m_syncManager(syncManager)
{
    if (m_feeAddress != "")
    {
        Error error = validateAddresses({m_feeAddress}, false);

        if (error != SUCCESS)
        {
            std::cout << WarningMsg("Fee address given is not valid: " + error.getErrorMessage()) << std::endl;
            exit(1);
        }
    }

    /* Route the request through our middleware function, before forwarding
       to the specified function */
    const auto router = [this](const auto function, const RpcMode routePermissions, const bool bodyRequired) {
        return [=](const httplib::Request &req, httplib::Response &res) {
            /* Pass the inputted function with the arguments passed through
               to middleware */
            middleware(
                req,
                res,
                routePermissions,
                bodyRequired,
                std::bind(function, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
            );
        };
    };

    const bool bodyRequired = true;
    const bool bodyNotRequired = false;

    m_server.Get("/info", router(&RpcServer::info, RpcMode::Default, bodyNotRequired))
            .Get("/fee", router(&RpcServer::fee, RpcMode::Default, bodyNotRequired))
            .Get("/height", router(&RpcServer::height, RpcMode::Default, bodyNotRequired))
            .Get("/peers", router(&RpcServer::peers, RpcMode::Default, bodyNotRequired))

            .Post("/sendrawtransaction", router(&RpcServer::sendTransaction, RpcMode::Default, bodyRequired))
            .Post("/getrandom_outs", router(&RpcServer::getRandomOuts, RpcMode::Default, bodyRequired))

            /* Matches everything */
            /* NOTE: Not passing through middleware */
            .Options(".*", [this](auto &req, auto &res) { handleOptions(req, res); });
}

RpcServer::~RpcServer()
{
    stop();
}

void RpcServer::start()
{
    m_serverThread = std::thread(&RpcServer::listen, this);
}

void RpcServer::listen()
{
    const auto listenError = m_server.listen(m_host, m_port);

    if (listenError != httplib::SUCCESS)
    {
        std::cout << WarningMsg("Failed to start RPC server: ")
                  << WarningMsg(httplib::detail::getSocketErrorMessage(listenError)) << std::endl;
        exit(1);
    }
}

void RpcServer::stop()
{
    m_server.stop();

    if (m_serverThread.joinable())
    {
        m_serverThread.join();
    }
}

std::tuple<std::string, uint16_t> RpcServer::getConnectionInfo()
{
    return {m_host, m_port};
}

void RpcServer::middleware(
    const httplib::Request &req,
    httplib::Response &res,
    const RpcMode routePermissions,
    const bool bodyRequired,
    std::function<std::tuple<Error, uint16_t>(
        const httplib::Request &req,
        httplib::Response &res,
        const rapidjson::Document &body)> handler)
{
    rapidjson::Document jsonBody;

    Logger::logger.log(
        "Incoming " + req.method + " request: " + req.path,
        Logger::DEBUG,
        { Logger::DAEMON_RPC }
    );

    if (m_corsHeader != "")
    {
        res.set_header("Access-Control-Allow-Origin", m_corsHeader);
    }

    if (bodyRequired && jsonBody.Parse(req.body.c_str()).HasParseError())
    {
        std::stringstream stream;

        if (!req.body.empty())
        {
            stream << "Warning: received body is not JSON encoded!\n"
                   << "Key/value parameters are NOT supported.\n"
                   << "Body:\n" << req.body;

            Logger::logger.log(
                stream.str(),
                Logger::INFO,
                { Logger::DAEMON_RPC }
            );
        }

        stream << "Failed to parse request body as JSON";

        failRequest(400, stream.str(), res);

        return;
    }

    /* If this route requires higher permissions than we have enabled, then
     * reject the request */
    if (routePermissions > m_rpcMode)
    {
        std::stringstream stream;

        stream << "You do not have permission to access this method. Please "
                  "relaunch your daemon with the --enable-blockexplorer";

        if (routePermissions == RpcMode::AllMethodsEnabled)
        {
            stream << "-detailed";
        }

        stream << " command line option to access this method.";

        failRequest(403, stream.str(), res);

        return;
    }

    try
    {
        const auto [error, statusCode] = handler(req, res, jsonBody);

        if (error)
        {
            rapidjson::StringBuffer sb;
            rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

            writer.StartObject();

            writer.Key("errorCode");
            writer.Uint(error.getErrorCode());

            writer.Key("errorMessage");
            writer.String(error.getErrorMessage());

            writer.EndObject();

            res.set_content(sb.GetString(), "application/json");
            res.status = 400;
        }
        else
        {
            res.status = statusCode;
        }

        return;
    }
    catch (const std::invalid_argument &e)
    {
        Logger::logger.log(
            "Caught JSON exception, likely missing required json parameter: " + std::string(e.what()),
            Logger::FATAL,
            { Logger::DAEMON_RPC }
        );

        failRequest(400, e.what(), res);
    }
    catch (const std::exception &e)
    {
        Logger::logger.log(
            "Caught unexpected exception: " + std::string(e.what()),
            Logger::FATAL,
            { Logger::DAEMON_RPC }
        );

        failRequest(500, "Internal server error: " + std::string(e.what()), res);
    }
}

void RpcServer::failRequest(uint16_t port, std::string body, httplib::Response &res)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("status");
    writer.String("Failed");

    writer.Key("error");
    writer.String(body);

    writer.EndObject();

    res.set_content(sb.GetString(), "application/json");
    res.status = port;
}

void RpcServer::handleOptions(const httplib::Request &req, httplib::Response &res) const
{
    Logger::logger.log(
        "Incoming " + req.method + " request: " + req.path,
        Logger::DEBUG,
        { Logger::DAEMON_RPC }
    );

    std::string supported = "OPTIONS, GET, POST";

    if (m_corsHeader == "")
    {
        supported = "";
    }

    if (req.has_header("Access-Control-Request-Method"))
    {
        res.set_header("Access-Control-Allow-Methods", supported);
    }
    else
    {
        res.set_header("Allow", supported);
    }

    if (m_corsHeader != "")
    {
        res.set_header("Access-Control-Allow-Origin", m_corsHeader);
        res.set_header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept, X-API-KEY");
    }

    res.status = 200;
}

std::tuple<Error, uint16_t> RpcServer::info(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    const uint64_t height = m_core->getTopBlockIndex() + 1;
    const uint64_t networkHeight = std::max(1u, m_syncManager->getBlockchainHeight());
    const auto blockDetails = m_core->getBlockDetails(height - 1);
    const uint64_t difficulty = m_core->getDifficultyForNextBlock();

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("height");
    writer.Uint64(height);

    writer.Key("difficulty");
    writer.Uint64(difficulty);

    writer.Key("tx_count");
    /* Transaction count without coinbase transactions - one per block, so subtract height */
    writer.Uint64(m_core->getBlockchainTransactionCount() - height);

    writer.Key("tx_pool_size");
    writer.Uint64(m_core->getPoolTransactionCount());

    writer.Key("alt_blocks_count");
    writer.Uint64(m_core->getAlternativeBlockCount());

    uint64_t total_conn = m_p2p->get_connections_count();
    uint64_t outgoing_connections_count = m_p2p->get_outgoing_connections_count();

    writer.Key("outgoing_connections_count");
    writer.Uint64(outgoing_connections_count);

    writer.Key("incoming_connections_count");
    writer.Uint64(total_conn - outgoing_connections_count);

    writer.Key("white_peerlist_size");
    writer.Uint64(m_p2p->getPeerlistManager().get_white_peers_count());

    writer.Key("grey_peerlist_size");
    writer.Uint64(m_p2p->getPeerlistManager().get_gray_peers_count());

    writer.Key("last_known_block_index");
    writer.Uint64(std::max(1u, m_syncManager->getObservedHeight()) - 1);

    writer.Key("network_height");
    writer.Uint64(networkHeight);

    writer.Key("upgrade_heights");
    writer.StartArray();
    {
        for (const uint64_t height : CryptoNote::parameters::FORK_HEIGHTS)
        {
            writer.Uint64(height);
        }
    }
    writer.EndArray();

    writer.Key("supported_height");
    writer.Uint64(CryptoNote::parameters::FORK_HEIGHTS_SIZE == 0
        ? 0
        : CryptoNote::parameters::FORK_HEIGHTS[CryptoNote::parameters::CURRENT_FORK_INDEX]);

    writer.Key("hashrate");
    writer.Uint64(round(difficulty / CryptoNote::parameters::DIFFICULTY_TARGET));

    writer.Key("synced");
    writer.Bool(height == networkHeight);

    writer.Key("major_version");
    writer.Uint64(blockDetails.majorVersion);

    writer.Key("minor_version");
    writer.Uint64(blockDetails.minorVersion);

    writer.Key("version");
    writer.String(PROJECT_VERSION);

    writer.Key("status");
    writer.String("OK");

    writer.Key("start_time");
    writer.Uint64(m_core->getStartTime());

    writer.EndObject();

    res.set_content(sb.GetString(), "application/json");

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::fee(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("address");
    writer.String(m_feeAddress);

    writer.Key("amount");
    writer.Uint64(m_feeAmount);

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.set_content(sb.GetString(), "application/json");

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::height(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("height");
    writer.Uint64(m_core->getTopBlockIndex() + 1);

    writer.Key("network_height");
    writer.Uint64(std::max(1u, m_syncManager->getBlockchainHeight()));

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.set_content(sb.GetString(), "application/json");

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::peers(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    std::list<PeerlistEntry> peers_white;
    std::list<PeerlistEntry> peers_gray;

    m_p2p->getPeerlistManager().get_peerlist_full(peers_gray, peers_white);

    writer.Key("peers");
    writer.StartArray();
    {
        for (const auto &peer : peers_white)
        {
            std::stringstream stream;
            stream << peer.adr;
            writer.String(stream.str());
        }
    }
    writer.EndArray();

    writer.Key("peers_gray");
    writer.StartArray();
    {
        for (const auto &peer : peers_gray)
        {
            std::stringstream stream;
            stream << peer.adr;
            writer.String(stream.str());
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.set_content(sb.GetString(), "application/json");

    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::sendTransaction(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    std::vector<uint8_t> transaction;

    const std::string rawData = getStringFromJSON(body, "tx_as_hex");

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    if (!Common::fromHex(rawData, transaction))
    {
        writer.Key("status");
        writer.String("Failed");

        writer.Key("error");
        writer.String("Failed to parse transaction from hex buffer");
    }
    else
    {
        Crypto::Hash transactionHash = Crypto::cn_fast_hash(transaction.data(), transaction.size());

        writer.Key("transactionHash");
        writer.String(Common::podToHex(transactionHash));

        std::stringstream stream;

        stream << "Attempting to add transaction " << transactionHash << " from /sendrawtransaction to pool";

        Logger::logger.log(
            stream.str(),
            Logger::DEBUG,
            { Logger::DAEMON_RPC }
        );

        const auto [success, error] = m_core->addTransactionToPool(transaction);

        if (!success)
        {
            /* Empty stream */
            std::stringstream().swap(stream);

            stream << "Failed to add transaction " << transactionHash << " from /sendrawtransaction to pool: " << error;

            Logger::logger.log(
                stream.str(),
                Logger::INFO,
                { Logger::DAEMON_RPC }
            );

            writer.Key("status");
            writer.String("Failed");

            writer.Key("error");
            writer.String(error);
        }
        else
        {
            m_syncManager->relayTransactions({transaction});

            writer.Key("status");
            writer.String("OK");

            writer.Key("error");
            writer.String("");

        }
    }

    writer.EndObject();

    res.set_content(sb.GetString(), "application/json");
    
    return {SUCCESS, 200};
}

std::tuple<Error, uint16_t> RpcServer::getRandomOuts(
    const httplib::Request &req,
    httplib::Response &res,
    const rapidjson::Document &body)
{
    const uint64_t numOutputs = getUint64FromJSON(body, "outs_count");

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();

    writer.Key("outs");

    writer.StartArray();
    {
        for (const auto &jsonAmount : getArrayFromJSON(body, "amounts"))
        {
            writer.StartObject();

            const uint64_t amount = jsonAmount.GetUint64();

            std::vector<uint32_t> globalIndexes;
            std::vector<Crypto::PublicKey> publicKeys;

            const auto [success, error] = m_core->getRandomOutputs(
                amount, static_cast<uint16_t>(numOutputs), globalIndexes, publicKeys
            );

            if (!success)
            {
                return {Error(CANT_GET_FAKE_OUTPUTS, error), 200};
            }

            if (globalIndexes.size() != numOutputs)
            {
                std::stringstream stream;

                stream << "Failed to get enough matching outputs for amount " << amount << " ("
                       << Utilities::formatAmount(amount) << "). Requested outputs: " << numOutputs
                       << ", found outputs: " << globalIndexes.size()
                       << ". Further explanation here: https://gist.github.com/zpalmtree/80b3e80463225bcfb8f8432043cb594c"
                       << std::endl
                       << "Note: If you are a public node operator, you can safely ignore this message. "
                       << "It is only relevant to the user sending the transaction.";

                return {Error(CANT_GET_FAKE_OUTPUTS, stream.str()), 200};
            }

            writer.Key("amount");
            writer.Uint64(amount);

            writer.Key("outs");
            writer.StartArray();
            {
                for (size_t i = 0; i < globalIndexes.size(); i++)
                {
                    writer.StartObject();
                    {
                        writer.Key("global_amount_index");
                        writer.Uint64(globalIndexes[i]);

                        writer.Key("out_key");
                        writer.String(Common::podToHex(publicKeys[i]));
                    }
                    writer.EndObject();
                }
            }
            writer.EndArray();

            writer.EndObject();
        }
    }
    writer.EndArray();

    writer.Key("status");
    writer.String("OK");

    writer.EndObject();

    res.set_content(sb.GetString(), "application/json");

    return {SUCCESS, 200};
}
