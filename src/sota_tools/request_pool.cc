#include "request_pool.h"

#include <chrono>
#include <exception>
#include <thread>

#include "logging/logging.h"

RequestPool::RequestPool(const TreehubServer& server, int max_curl_requests)
    : rate_controller_(max_curl_requests), running_requests_(0), server_(server), stopped_(false) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  multi_ = curl_multi_init();
  curl_multi_setopt(multi_, CURLMOPT_PIPELINING, CURLPIPE_HTTP1 | CURLPIPE_MULTIPLEX);
}

RequestPool::~RequestPool() {
  Abort();

  LOG_INFO << "Shutting down RequestPool...";
  while (!is_idle()) {
    LoopListen();
  }
  LOG_INFO << "...done";

  curl_multi_cleanup(multi_);
  curl_global_cleanup();
}

void RequestPool::AddQuery(const OSTreeObject::ptr& request) {
  request->LaunchNotify();
  if (!stopped_) {
    query_queue_.push_back(request);
  }
}

void RequestPool::AddUpload(const OSTreeObject::ptr& request) {
  request->LaunchNotify();
  if (!stopped_) {
    upload_queue_.push_back(request);
  }
}

void RequestPool::LoopLaunch(const bool dryrun) {
  while (running_requests_ < rate_controller_.MaxConcurrency() && (!query_queue_.empty() || !upload_queue_.empty())) {
    OSTreeObject::ptr cur;

    // Queries first, uploads second
    if (query_queue_.empty()) {
      cur = upload_queue_.front();
      upload_queue_.pop_front();
      cur->Upload(server_, multi_, dryrun);
      total_requests_made_++;
      if (dryrun) {
        // Don't send an actual upload message, just skip to the part where we
        // acknowledge that the object has been uploaded.
        cur->NotifyParents(*this);
      }
    } else {
      cur = query_queue_.front();
      query_queue_.pop_front();
      cur->MakeTestRequest(server_, multi_);
      total_requests_made_++;
    }

    running_requests_++;
  }
}

void RequestPool::LoopListen() {
  CURLMcode mc;
  // Poll for IO
  fd_set fdread, fdwrite, fdexcept;
  int maxfd = 0;
  FD_ZERO(&fdread);
  FD_ZERO(&fdwrite);
  FD_ZERO(&fdexcept);
  long timeoutms = 0;  // NOLINT
  mc = curl_multi_timeout(multi_, &timeoutms);
  if (mc != CURLM_OK) {
    throw std::runtime_error("curl_multi_timeout failed with error");
  }
  struct timeval timeout {};
  timeout.tv_sec = timeoutms / 1000;
  timeout.tv_usec = 1000 * (timeoutms % 1000);

  mc = curl_multi_fdset(multi_, &fdread, &fdwrite, &fdexcept, &maxfd);
  if (mc != CURLM_OK) {
    throw std::runtime_error("curl_multi_fdset failed with error");
  }

  if (maxfd != -1) {
    select(maxfd + 1, &fdread, &fdwrite, &fdexcept, timeoutms == -1 ? nullptr : &timeout);
  } else {
    LOG_DEBUG << "Waiting 100ms for curl";
    // If maxfd == -1, then wait 100ms. See:
    // https://curl.haxx.se/libcurl/c/curl_multi_timeout.html
    timeout.tv_sec = 0;
    timeout.tv_usec = 100 * 1000;
    select(0, nullptr, nullptr, nullptr, &timeout);
  }

  // Ask curl to handle IO
  mc = curl_multi_perform(multi_, &running_requests_);
  if (mc != CURLM_OK) {
    throw std::runtime_error("curl_multi failed with error");
  }

  // Deal with any completed requests
  int msgs_in_queue;
  do {
    CURLMsg* msg = curl_multi_info_read(multi_, &msgs_in_queue);
    if ((msg != nullptr) && msg->msg == CURLMSG_DONE) {
      OSTreeObject::ptr h = ostree_object_from_curl(msg->easy_handle);
      h->CurlDone(multi_, *this);
      const bool server_responded_ok = h->LastOperationResult() == ServerResponse::kOk;
      const RateController::clock::time_point start_time = h->RequestStartTime();
      const RateController::clock::time_point end_time = RateController::clock::now();
      rate_controller_.RequestCompleted(start_time, end_time, server_responded_ok);
      if (rate_controller_.ServerHasFailed()) {
        Abort();
      } else {
        std::this_thread::sleep_for(rate_controller_.GetSleepTime());
      }
    }
  } while (msgs_in_queue > 0);
}

void RequestPool::Loop(const bool dryrun) {
  LoopLaunch(dryrun);
  LoopListen();
}
// vim: set tabstop=2 shiftwidth=2 expandtab:
