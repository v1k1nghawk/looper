/**********************************************************************************\
* File name:            looper.cpp                                                 *
* Purpose:              A switching (bridge) loop detection by a broadcast storm   *
* Programmer:           Andrew Artemev                                             *
* Last modified date:   26.06.2020                                                 *
\**********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <iterator>
#include <exception>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/chrono.hpp>
#include <boost/lexical_cast.hpp>


#define VERSION_MAJOR  1
#define VERSION_MINOR  2

#define UDPDEST_PORT   4950


const std::string PROGNAME = "looper";

const std::string snd_payload("broadcastTESTmsg_" + PROGNAME);

// program's initial settings
bool bsilent = 0;
bool bverbose = 0;
unsigned int deadline = 0;

// synchronization
bool bmanager_ready = 0;
bool listener_ready = 0;
boost::mutex print_mutex;

// interaction failed
bool terminate_io = 0;

// result flag
bool bloop_found = 0;

// not many socket connections => one soc ops handler
boost::asio::io_service ios;

// db with sender's IP and its msg counter
std::unordered_map<std::string, unsigned int> ip_catches;


/**
 * Concurrency control printing with a program name as a prefix
 *
 * @param[in] str_to_cout Printable data
 * @param[in] mode Type of printing (0 - always, 1 - print if it's not silent, 2 - print if it's verbose)
 */
void sync_print(const std::string & str_to_cout, short mode=0)
{
   if ( (mode == 0) || ((mode == 1) && (!bsilent)) || ((mode == 2) && (bverbose)) )
   {
	  print_mutex.lock();
      std::cout << PROGNAME << ": " << str_to_cout << std::endl;
      print_mutex.unlock();
   }
}

void wait(unsigned int secs)
{
   try {
      boost::this_thread::sleep_for(boost::chrono::seconds(secs));
   } catch(const boost::thread_interrupted & interrupt) {
      sync_print("interrupted", 1);
      terminate_io = 1;
   }
}

enum args_code {
    esilent,
	everbose,
    ehelp
};

/**
 * Conversion a parameter to its code
 *
 * @param[in] argtext Command-line argument of a program
 * @return an appropriate code of that argument
 */
args_code hashargs(const std::string & argtext) {
	if ( (argtext == "-s") || (argtext == "--silent") ) return esilent;
	if ( (argtext == "-v") || (argtext == "--verbose") ) return everbose;
    if ( (argtext == "-h") || (argtext == "--help") ) return ehelp;
    // default
    return ehelp;
}

/**
 * Actions on exit
 *
 */
void finalizer()
{
	// print finds

	if(terminate_io)
		sync_print("ERRORS ENCOUNTERED. EXIT.", 1);

	if(!bsilent)
	{
	   if(ip_catches.size() > 0)
	   {
		   sync_print("Results:", 1);
		   for(auto & elem : ip_catches)
		   {
			  sync_print(boost::lexical_cast<std::string>(elem.second) + " message(s) from IP " + elem.first, 1);
		   }
	   }
	}

	if(bloop_found)
	{
       sync_print("LOOP FOUND", 1);
       std::_Exit(EXIT_FAILURE);
	}
    else
    {
       sync_print("LOOP NOT FOUND", 1);
       std::_Exit(EXIT_SUCCESS);
    }
}

void help(const std::string & binpath)
{
	sync_print("version: " + boost::lexical_cast<std::string>(VERSION_MAJOR) + "." + boost::lexical_cast<std::string>(VERSION_MINOR), 0);
	sync_print("purpose: a switching loop detection by a broadcast storm", 0);
	sync_print("usage: " + binpath + " [[-s|--silent [deadline]] | [-v|--verbose] | [-h|--help]]", 0);

	std::_Exit(EXIT_SUCCESS);
}


void tlistener_udp()
{
	// wait for broadcast manager
	while(!bmanager_ready)
       wait(1);

	sync_print("listener: started", 2);

	bool first_time = 1;

	try {
	   boost::asio::ip::udp::socket rcv_sock(ios);

	   rcv_sock.open(boost::asio::ip::udp::v4());

	   // 0.0.0.0
	   boost::asio::ip::udp::endpoint rcv_ep = boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::any(), UDPDEST_PORT);

	   rcv_sock.set_option(boost::asio::ip::udp::socket::reuse_address(true));

	   rcv_sock.bind(rcv_ep);

	   // receive data
       for(;;)
       {
    	  if(terminate_io)
    	     return;

    	  // reaction on catch
    	  if(!bloop_found)
    	  {
    	     // evidence of a detected switching loop
    	     if(std::accumulate(ip_catches.begin(), ip_catches.end(), 0,
                [](const unsigned int prev, const std::pair<std::string, unsigned int>& elem)
                {return prev + elem.second;}) > ip_catches.size())
    	     {
    	    	bloop_found = 1;
    	    	if(bsilent)
        	       break;
    	     }
    	  }

    	  boost::asio::ip::udp::endpoint sender_ep;
    	  boost::system::error_code err;

    	  std::vector<char> rcv_payload(snd_payload.size(), '\0');

	      std::size_t rcv_bytes = 0;

	      if (first_time == 1)
	      {
			 first_time = 0;

	    	 sync_print("listener: ready", 2);

	         // activate broadcasters
	    	 listener_ready = 1;
	      }

          rcv_bytes = rcv_sock.receive_from(boost::asio::buffer(rcv_payload), sender_ep, 0, err);

	      if (err && err != boost::asio::error::message_size)
	         throw boost::system::system_error(err);

	      // counting app's broadcast messages
	      if(std::string(rcv_payload.begin(), rcv_payload.end()) == snd_payload)
	      {
	         std::unordered_map<std::string, unsigned int>::iterator ip_cat_iter = ip_catches.find(sender_ep.address().to_string());

	         if(ip_cat_iter != ip_catches.end())
	            ip_cat_iter->second++;
	         else
	        	ip_catches.insert(std::make_pair(sender_ep.address().to_string(), 1));

		     sync_print("listener: got message from " + boost::lexical_cast<std::string>(sender_ep) +
		    		 " count " + boost::lexical_cast<std::string>(ip_catches.at(sender_ep.address().to_string())), 1);
	      }

       }

	} catch (const std::exception& ex) {
       sync_print("listener: " + boost::lexical_cast<std::string>(ex.what()), 1);
       terminate_io = 1;
    }

}

void tbroadcast_udp(boost::asio::ip::udp::endpoint source_ep)
{
	// wait for listener
	for(;;)
	{
	   if(terminate_io)
		  return;
	   else	if(listener_ready)
	      break;
	   else
	      wait(1);
	}

	sync_print("broadcaster on " + boost::lexical_cast<std::string>(source_ep) + ": started", 2);

    boost::system::error_code snd_err;

    // active socket
    boost::asio::ip::udp::socket snd_sock(ios);

    // a UDP protocol with IPv4 as underlying protocol
    boost::asio::ip::udp protocol = boost::asio::ip::udp::v4();

    snd_sock.open(protocol, snd_err);
    if (snd_err.value())
    {
       sync_print("broadcaster on " + boost::lexical_cast<std::string>(source_ep) +
    		   ": failed to open a sender socket. Error code " +
			   boost::lexical_cast<std::string>(snd_err.value()) +
			   ". " + snd_err.message(), 1);
       terminate_io = 1;
    }

    snd_sock.set_option(boost::asio::ip::udp::socket::reuse_address(true));
    snd_sock.set_option(boost::asio::socket_base::broadcast(true));

    snd_sock.bind(source_ep, snd_err);
    if (snd_err.value())
    {
       sync_print("broadcaster on " + boost::lexical_cast<std::string>(source_ep) +
    		   ": failed to bind a source endpoint to a sender socket. Error code " +
			   boost::lexical_cast<std::string>(snd_err.value()) + ". " + snd_err.message(), 1);
       terminate_io = 1;
    }

    boost::asio::ip::udp::endpoint dest_ep(boost::asio::ip::address_v4::broadcast(), UDPDEST_PORT);

    // send data
    try {
       sync_print("broadcaster on " + boost::lexical_cast<std::string>(source_ep) + ": sending message to " + boost::lexical_cast<std::string>(dest_ep), 1);
       snd_sock.send_to(boost::asio::buffer(snd_payload, snd_payload.size()), dest_ep);
       sync_print("broadcaster on " + boost::lexical_cast<std::string>(source_ep) + ": message has been sent successfully", 2);
    } catch (const std::exception& ex) {
       sync_print("broadcaster on " + boost::lexical_cast<std::string>(source_ep) + ": " + boost::lexical_cast<std::string>(ex.what()), 1);
    }

    snd_sock.close(snd_err);
    if (snd_err.value())
    {
       sync_print("broadcaster on " + boost::lexical_cast<std::string>(source_ep) +
    		   ": failed to close the sender socket. Error code " +
			   boost::lexical_cast<std::string>(snd_err.value()) +
			   ". " + snd_err.message(), 1);
       terminate_io = 1;
    }
}

void tbroadcast_udp_mng()
{
	sync_print("broadcast manager: started", 2);

	boost::thread_group tgbroadcastersudp;

	using boost::asio::ip::udp;

	udp::resolver resolver(ios);

	sync_print("hostname: " + boost::asio::ip::host_name(), 1);
	udp::resolver::query query(boost::asio::ip::host_name(), boost::lexical_cast<std::string>(UDPDEST_PORT));
	udp::resolver::iterator iterator = resolver.resolve(query);
	udp::resolver::iterator end_iterator;

	// spawn a dedicated broadcaster on every interface assigned to host name
	while(iterator != end_iterator)
	{
	   udp::endpoint current_iface_ep = *iterator++;

	   sync_print("IP by hostname: " + current_iface_ep.address().to_string(), 1);

	   // on the heap
	   tgbroadcastersudp.create_thread(boost::bind(tbroadcast_udp, current_iface_ep));
	}

	// activate listener
	bmanager_ready = 1;

	// wait for derived threads
	tgbroadcastersudp.join_all();
}


int main(int argc, char* argv[])
{
	// handler
	std::set_terminate(finalizer);

	// analyze app's arguments
	if ( (argc == 2) || (argc == 3) )
	{
	   switch (hashargs(std::string(argv[1])))
	   {
	      case esilent:
		      bsilent = 1;

		      if(argc == 3)
		      {
		         try
		         {
		        	deadline = boost::lexical_cast<unsigned int>(argv[2]) * 1000;
		         }
		         catch (const boost::bad_lexical_cast & bad_cast)
		         {
		        	help(argv[0]);
		         }
		      }
		      break;
	      case everbose:
	    	  sync_print("Verbose mode is on");
		      bverbose = 1;
		      break;
	      case ehelp:
		      help(argv[0]);
		      break;

	      default:
		      break;
	   }
	}
	else if (argc > 3)
	{
		help(argv[0]);
	}

	// network interaction
	boost::thread broadcaster(&tbroadcast_udp_mng);
	boost::thread listener(&tlistener_udp);

	broadcaster.join();
	if(!deadline)        // infinite
		listener.join();
	else                 // timed
	    listener.timed_join(boost::posix_time::milliseconds(deadline));

	// finalize
	std::terminate();
}
