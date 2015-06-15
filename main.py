import os
import socket
import time
import sys
sys.path.insert(0, '/src')
sys.path.insert(0, '/src/sandstorm')
import capnp
import util_capnp
import grain_capnp
import hack_session_capnp
import api_session_capnp
import sandstorm_http_bridge_capnp
# import web_session_capnp

import thread
import BaseHTTPServer

PORT = 10000

class HttpServer(BaseHTTPServer.BaseHTTPRequestHandler):
    def do_GET(self):
      global session_id
      self.send_response(200)
      self.send_header('Content-type','text/html')
      self.end_headers()
      self.wfile.write("Hello World !")
      session_id = self.headers["X-Sandstorm-Session-Id"]
      return

class OngoingNotification(grain_capnp.OngoingNotification.Server):
  def __del__(self):
    print >>sys.stderr, 'deleting notification'

  def cancel(self, **kwargs):
    print >>sys.stderr, 'cancelling notification'
    os._exit(0)

httpd = BaseHTTPServer.HTTPServer(("", PORT), HttpServer)

print "serving at port", PORT
thread.start_new_thread(lambda: httpd.serve_forever(), ())

time.sleep(1)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect("/tmp/sandstorm-api")
c = capnp.TwoPartyClient(s)
print >>sys.stderr, 'test3'
cap = c.bootstrap().cast_as(sandstorm_http_bridge_capnp.SandstormHttpBridge).getSandstormApi().api.cast_as(grain_capnp.SandstormApi)

print >>sys.stderr, 'test'
wakelock = cap.stayAwake(notification=OngoingNotification(), displayInfo={"caption": {"defaultText": "background test"}}).wait()
print >>sys.stderr, 'test2'
print >>sys.stderr, wakelock
time.sleep(15)

del wakelock
capnp.wait_forever()
