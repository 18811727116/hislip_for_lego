#include <vector>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

#include <QDebug>

namespace hislip{
  using boost::asio::ip::tcp;

  class tcp_connection;
  
  class session{
  public:
    session(uint16_t id, QString name){
      _id = id;
      _name = name;
    }
    uint16_t id(){
      return _id;
    }

    void set_sync_connection(tcp_connection* conn){
      _sync_conn = conn;
    }

    void set_async_connection(tcp_connection* conn){
      _async_conn = conn;
    }

  private:
    QString         _name;
    uint16_t        _id;
    tcp_connection* _sync_conn;
    tcp_connection* _async_conn;
  };

  class session_manager{
  public:
    session_manager(){
    }

    ~session_manager(){
      for (auto s : _sessions){
        delete s;
      }
      _sessions.clear();
    }

    session* new_session(QString name){
      session* s = new session(_curr_id, name);

      _sessions.push_back(s);
      qDebug("create new session id=%d", _curr_id);
      _curr_id++;
      return s;
    }

    void remvoe_session(uint16_t id){
      std::vector<session* > ::iterator it = _sessions.begin();
      for(; it != _sessions.end();){
        if((*it)->id() == id){
          it = _sessions.erase(it);
        }else{
          ++it;
        }
      }
    }

    session* have_session(uint16_t id){
      for (auto s : _sessions){
        if (s->id() == id)
          return s;
      }
      return nullptr;
    }
    
  private:
    std::vector<session* >  _sessions;
    uint16_t                _curr_id = 0x100;
  };

  session_manager session_mgr;
  
  class tcp_connection
    : public boost::enable_shared_from_this<tcp_connection>
  {
  public:
    typedef boost::shared_ptr<tcp_connection> pointer;

    typedef enum msg_type
    {
        Initialize,
        InitializeResponse,
        FatalError,
        Error,
        AsyncLock,
        AsyncLockResponse,
        Data,
        DataEnd,
        DeviceClearComplete,
        DeviceClearAcknowledge,
        AsyncRemoteLocalControl,
        AsyncRemoteLocalResponse,
        Trigger,
        Interrupted,
        AsyncInterrupted,
        AsyncMaximumMessageSize,
        AsyncMaximumMessageSizeResponse,
        AsyncInitialize,
        AsyncInitializeResponse,
        AsyncDeviceClear,
        AsyncServiceRequest,
        AsyncStatusQuery,
        AsyncStatusResponse,
        AsyncDeviceClearAcknowledge,
        AsyncLockInfo,
        AsyncLockInfoResponse
    }msg_type;

    typedef struct msg_header{
        uint16_t prologue;
        uint8_t type;
        uint8_t control_code;
        union{
          struct {
            uint16_t upper;
            uint16_t lower;
          }s;
          uint32_t timeout;
          uint32_t parameter;
          uint32_t message_id;
        }parameter;
        uint64_t payload_length;
    }msg_header ;

    const uint16_t c_prologue  = 0x4853; /* HS */
    const uint16_t c_vendor_id = 0x575A; /* WZ */
    const uint16_t c_version   = 0x0100;
    
    enum packet_status{
        IN_HEADER,      //! reading message header
        IN_PAYLOAD,      //! reading message payload
    };

    static pointer create(boost::asio::io_service& io_service)
    {
      return pointer(new tcp_connection(io_service));
    }

    tcp::socket& socket()
    {
      return socket_;
    }

    void start()
    {
      read_header();
    }

    void stop(){
      socket().close();
    }

    void do_write(size_t l){
        boost::asio::async_write(socket_, boost::asio::buffer(_write_buffer, l),
                                 boost::bind(&tcp_connection::handle_write, shared_from_this(),
                                             boost::asio::placeholders::error,
                                             boost::asio::placeholders::bytes_transferred));
    }

    void send_resp(void){
      msg_header *resp = (msg_header*)_write_buffer;
      
      size_t payload_lengh = resp->payload_length;
      resp->payload_length = htonll(payload_lengh);
      
      do_write(sizeof(msg_header) + payload_lengh);
    }

    void do_read(size_t offset, size_t l){
       boost::asio::async_read(socket_, boost::asio::buffer(_read_buffer + offset, l),
                                         boost::bind(&tcp_connection::on_read, shared_from_this(),
                                                     boost::asio::placeholders::error,
                                                     boost::asio::placeholders::bytes_transferred));
    }

    void read_header(){
      _recv_status = IN_HEADER;
      do_read(0, sizeof(msg_header));
    }

    void read_payload(size_t l){
      _recv_status = IN_PAYLOAD;
      do_read(sizeof(msg_header), l);
    }
    
    void on_read(const boost::system::error_code & err, size_t bytes){
      msg_header* hdr = (msg_header*)_read_buffer;
      
      // check socket status
      if(err && err != boost::asio::error::message_size){
          if (err == boost::asio::error::eof)
              qDebug("data conn closed: %s" ,err.message().c_str());
          else
              qDebug("data have err: %s", err.message().c_str());
          
          stop();
          return;
      }

      // process header
      if (_recv_status == IN_HEADER){
        // check header
        if (not hdl_msg_hdr(hdr)){
          qWarning("message header incorrect!");
          return;
        }
        
        // read payload
        if(hdr->payload_length <= _payload_buff_size){
          read_payload(hdr->payload_length);
        }else{
          qWarning("local buffer overflow, expect length is %lu", hdr->payload_length);
        }
        return;
      }

      
      // process messages
      switch(hdr->type){
        case Initialize:  hdl_initialize(hdr); break;
        case AsyncInitialize: hdl_async_initialize(hdr); break;
        case AsyncMaximumMessageSize: hdl_async_maximum_message_size(hdr); break;
        case AsyncLock: hdl_async_lock(hdr); break;
        case DataEnd: hdl_data_end(hdr); break;
        case AsyncLockInfo: hdl_async_lock(hdr); break;
        
        default:{
          qWarning("unknow message type %d", hdr->type);
        }
      }

      // read next header
      read_header();
    }

    void hdl_initialize(msg_header* hdr){
      
      // check name
      char *p = (char*)payload_pos(hdr);
      *(p + hdr->payload_length) = '\0';
      QString name = QString::asprintf("%s", p); 

      if (name.startsWith("hislip", Qt::CaseInsensitive)){
        // create session
        session* s = session_mgr.new_session(name);
        s->set_sync_connection(this);

        // create response
        msg_header* resp = init_header(InitializeResponse, 0x01);
        resp->parameter.s.upper = htons(c_version);
        resp->parameter.s.lower = htons(s->id()); // session id

        _is_sync_channel  = true;
        _session_id       = s->id();
        
      }else{
      
        init_header(FatalError);
      }
      
      send_resp();
    }

    void hdl_async_initialize(msg_header* hdr){
      uint16_t id = ntohs(hdr->parameter.s.lower);

      session* s = session_mgr.have_session(id);
      if (s){
        s->set_async_connection(this);
        
        msg_header* resp = init_header(AsyncInitializeResponse);
        resp->parameter.s.lower = htons(c_vendor_id);

        _is_sync_channel  = false;
        _session_id       = s->id();
        
      }else{
        init_header(FatalError);
      }
      send_resp();
    }

    void hdl_async_maximum_message_size(msg_header* req){
      uint64_t req_size = htonll(*(uint64_t*)payload_pos(req));

      if(realloc_buffer(req_size)){
        msg_header* resp = init_header(AsyncMaximumMessageSizeResponse);
        
        uint64_t* prespsize = (uint64_t*)payload_pos(resp);
        *prespsize = htonll(req_size);
        resp->payload_length = sizeof(uint64_t);
      }else{
        qWarning("can not alloc new size");
      }
      send_resp();
    }

    void hdl_async_lock(msg_header* hdr){
      uint8_t cc = ntohs(hdr->control_code);
      uint32_t timeout = ntohl(hdr->parameter.timeout);

      if (cc == 0x01){        
        msg_header* resp = init_header(AsyncLockResponse);
        resp->control_code = 0x01; /* success */
      }else{
        init_header(FatalError);
      }
      send_resp();
    };

    void hdl_data_end(msg_header* hdr){

      char *p = (char*)payload_pos(hdr);
      *(p + hdr->payload_length) = '\0';
      QString name = QString::asprintf("%s", p); 
      qDebug("recv: %s", qPrintable(name.trimmed()));
      
      QString idn("Welzek Tech, T6290,CNaaa");
      
      msg_header* resp = init_header(DataEnd);
      resp->parameter.message_id = hdr->parameter.message_id;
      
      strcpy((char*)payload_pos(resp), idn.toLatin1());
      resp->payload_length = idn.size();
      send_resp();
    }
    
    msg_header* init_header(msg_type type, uint8_t cc=0){
      msg_header* resp = (msg_header*)_write_buffer;
      
      resp->prologue = htons(c_prologue);
      resp->type = type;
      resp->control_code = cc;
      resp->parameter.parameter = 0;
      resp->payload_length = 0;
      
      return resp;
    }

    bool hdl_msg_hdr(msg_header* hdr){
      if(hdr){
        hdr->prologue = ntohs(hdr->prologue);
        hdr->payload_length = htonll(hdr->payload_length);
          
        if (hdr->prologue == c_prologue)
          return true;
      }
      
      return false;
    }

    uint8_t* payload_pos(msg_header* hdr){
      return (uint8_t*)hdr + sizeof(msg_header);
    }

    uint64_t htonll(uint64_t value)
    {
        // The answer is 42
        static const int num = 42;
    
        // Check the endianness
        if (*reinterpret_cast<const char*>(&num) == num)
        {
            const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
            const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));
    
            return (static_cast<uint64_t>(low_part) << 32) | high_part;
        }
        else
            return value;
    }

  private:
    tcp_connection(boost::asio::io_service& io_service)
      : socket_(io_service)
    {
      _recv_status = IN_HEADER;
      
      _payload_buff_size = 2*1024*1024;
      _read_buffer  = (char*)malloc(_payload_buff_size);
      _write_buffer = (char*)malloc(_payload_buff_size);


      _session_id       = 0;
      _is_sync_channel  = true;
    }

    bool realloc_buffer(size_t size){
    
      if (size > _payload_buff_size){
      
        _read_buffer = (char*)realloc(_read_buffer, size + sizeof(msg_header));
        _write_buffer = (char*)realloc(_write_buffer, size + sizeof(msg_header));

        if(_read_buffer && _write_buffer)
          _payload_buff_size = size;
        else
          return false;
      }
      
      return true;
    }

    void handle_write(const boost::system::error_code& /*error*/,
        size_t /*bytes_transferred*/)
    {
    }

    tcp::socket   socket_;

    uint16_t      _session_id;
    bool          _is_sync_channel;
    
    packet_status _recv_status;
    
    size_t        _payload_buff_size;
    char*         _read_buffer;
    char*         _write_buffer;
  };

  class tcp_server
  {
  public:
    tcp_server(boost::asio::io_service& io_service)
      : acceptor_(io_service, tcp::endpoint(tcp::v4(), 4880))
    {
      start_accept();
    }

  private:
    void start_accept()
    {
      tcp_connection::pointer new_connection =
        tcp_connection::create(acceptor_.get_io_service());

      acceptor_.async_accept(new_connection->socket(),
          boost::bind(&tcp_server::handle_accept, this, new_connection,
            boost::asio::placeholders::error));
    }

    void handle_accept(tcp_connection::pointer new_connection,
        const boost::system::error_code& error)
    {
      if (!error)
      {
        new_connection->start();
      }

      start_accept();
    }

    tcp::acceptor acceptor_;
  };
};

int main()
{
  try
  {
    boost::asio::io_service io_service;
    hislip::tcp_server server(io_service);

    qDebug("run server");
    io_service.run();
  }
  catch (std::exception& e)
  {
    qFatal("%s", e.what());
  }

  return 0;
}

