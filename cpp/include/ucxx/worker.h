/**
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See file LICENSE for terms.
 */
#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <thread>

#include <ucp/api/ucp.h>

#include <ucxx/component.h>
#include <ucxx/constructors.h>
#include <ucxx/context.h>
#include <ucxx/delayed_submission.h>
#include <ucxx/inflight_requests.h>
#include <ucxx/worker_progress_thread.h>

#include <ucxx/python/typedefs.h>

#if UCXX_ENABLE_PYTHON
#include <ucxx/python/notifier.h>
#include <ucxx/python/python_future.h>
#endif

namespace ucxx {

class Address;
class Endpoint;
class Listener;

namespace python {

class Future;

}  // namespace python

class Worker : public Component {
 private:
  ucp_worker_h _handle{nullptr};  ///< The UCP worker handle
  int _epollFileDescriptor{-1};   ///< The epoll file descriptor
  int _workerFileDescriptor{-1};  ///< The worker file descriptor
  std::shared_ptr<InflightRequests> _inflightRequests{
    std::make_shared<InflightRequests>()};  ///< The inflight requests
  std::shared_ptr<InflightRequests> _inflightRequestsToCancel{
    std::make_shared<InflightRequests>()};  ///< The inflight requests scheduled to be canceled
  std::shared_ptr<WorkerProgressThread> _progressThread{nullptr};  ///< The progress thread object
  std::function<void(void*)> _progressThreadStartCallback{
    nullptr};  ///< The callback function to execute at progress thread start
  void* _progressThreadStartCallbackArg{
    nullptr};  ///< The argument to be passed to the progress thread start callback
  std::shared_ptr<DelayedSubmissionCollection> _delayedSubmissionCollection{
    nullptr};  ///< Collection of enqueued delayed submissions
  bool _enablePythonFuture{
    false};  ///< Boolean identifying whether the worker was created with Python future capability
#if UCXX_ENABLE_PYTHON
  std::mutex _pythonFuturesPoolMutex{};  ///< Mutex to access the Python futures pool
  std::queue<std::shared_ptr<ucxx::python::Future>>
    _pythonFuturesPool{};  ///< Python futures pool to prevent running out of fresh futures
  std::shared_ptr<ucxx::python::Notifier> _notifier{
    ucxx::python::createNotifier()};  ///< Python notifier object
#endif

  /**
   * @brief Private constructor of `ucxx::Worker`.
   *
   * This is the internal implementation of `ucxx::Worker` constructor, made private not
   * to be called directly. Instead the user should call `context::createWorker()` or
   * `ucxx::createWorker()`.
   *
   *
   * @param[in] context the context from which to create the worker.
   * @param[in] enableDelayedSubmission if `true`, each `ucxx::Request` will not be
   *                                    submitted immediately, but instead delayed to
   *                                    the progress thread. Requires use of the
   *                                    progress thread.
   * @param[in] enablePythonFuture if `true`, notifies the Python future associated
   *                               with each `ucxx::Request`. Requires UCXX built with
   *                               `-DUCXX_ENABLE_PYTHON=1`.
   */
  Worker(std::shared_ptr<Context> context,
         const bool enableDelayedSubmission = false,
         const bool enablePythonFuture      = false);

  /**
   * @brief Drain the worker for uncatched tag messages received.
   *
   * Called by the destructor, any uncatched tag messages received will be drained so as
   * not to generate UCX warnings.
   */
  void drainWorkerTagRecv();

  /**
   * @brief Stop the progress thread if running without raising warnings.
   *
   * Called by the destructor, will stop the progress thread if running without
   * raising warnings.
   */
  void stopProgressThreadNoWarn();

  /**
   * @brief Register an inflight request.
   *
   * Called each time a new transfer request is made by the `Worker`, such that it may
   * be canceled when necessary.
   *
   * @param[in] request the request to register.
   */
  void registerInflightRequest(std::shared_ptr<Request> request);

 public:
  Worker()              = delete;
  Worker(const Worker&) = delete;
  Worker& operator=(Worker const&) = delete;
  Worker(Worker&& o)               = delete;
  Worker& operator=(Worker&& o) = delete;

  /**
   * @brief Constructor of `shared_ptr<ucxx::Worker>`.
   *
   * The constructor for a `shared_ptr<ucxx::Worker>` object. The default constructor is
   * made private to ensure all UCXX objects are shared pointers and correct
   * lifetime management.
   *
   * @code{.cpp}
   * // context is `std::shared_ptr<ucxx::Context>`
   * auto worker = context->createWorker(false, false);
   *
   * // Equivalent to line above
   * // auto worker = ucxx::createWorker(context, false, false);
   * @endcode
   *
   * @param[in] context the context from which to create the worker.
   * @param[in] enableDelayedSubmission if `true`, each `ucxx::Request` will not be
   *                                    submitted immediately, but instead delayed to
   *                                    the progress thread. Requires use of the
   *                                    progress thread.
   * @param[in] enablePythonFuture if `true`, notifies the Python future associated
   *                               with each `ucxx::Request`. Requires UCXX built with
   *                               `-DUCXX_ENABLE_PYTHON=1`.
   * @returns The `shared_ptr<ucxx::Worker>` object
   */
  friend std::shared_ptr<Worker> createWorker(std::shared_ptr<Context> context,
                                              const bool enableDelayedSubmission,
                                              const bool enablePythonFuture);

  /**
   * @brief `ucxx::Worker` destructor.
   */
  ~Worker();

  /**
   * @brief Get the underlying `ucp_worker_h` handle.
   *
   * Lifetime of the `ucp_worker_h` handle is managed by the `ucxx::Worker` object and
   * its ownership is non-transferrable. Once the `ucxx::Worker` is destroyed the handle
   * is not valid anymore, it is the user's responsibility to ensure the owner's lifetime
   * while using the handle.
   *
   * @code{.cpp}
   * // worker is `std::shared_ptr<ucxx::Worker>`
   * ucp_worker_h workerHandle = worker->getHandle();
   * @endcode
   *
   * @returns The underlying `ucp_worker_h` handle.
   */
  ucp_worker_h getHandle();

  /**
   * @brief Get information about the underlying `ucp_worker_h` object.
   *
   * Convenience wrapper for `ucp_worker_print_info()` to get information about the
   * underlying UCP worker handle and return it as a string.
   *
   * @returns String containing information about the UCP worker.
   */
  std::string getInfo();

  /**
   * @brief Initialize blocking progress mode.
   *
   * Initialize blocking progress mode, creates internal file descriptors to handle blocking
   * progress by waiting for the UCP worker to notify the file descriptors. This method is
   * supposed to be callde when usage of `progressWorkerEvent()` is intended, before the
   * first call to `progressWorkerEvent()`. If using polling mode only via
   * `progress()`/`progressOnce()` calls or wake-up with `waitProgress()`, this method should
   * not be called.
   *
   * In blocking mode, the user should call `progressWorkerEvent()` to block and then progress
   * the worker as new events arrive. `wakeProgressEvent()` may be called to forcefully wake
   * this method, for example to shutdown the application.
   *
   * @code{.cpp}
   * // worker is `std::shared_ptr<ucxx::Worker>`
   *
   * // Block until UCX's wakes up for an incoming event, then fully progresses the
   * // worker
   * worker->initBlockingProgressMode();
   * worker->progressWorkerEvent();
   *
   * // All events have been progressed.
   * @endcode
   *
   * @throws std::ios_base::failure if creating any of the file descriptors or setting their
   *                                statuses.
   */
  void initBlockingProgressMode();

  /**
   * @brief Arm the UCP worker.
   *
   * Wrapper for `ucp_worker_arm`, checking its return status for errors and raising an
   * exception if an error occurred.
   *
   * @throws ucxx::Error if an error occurred while attempting to arm the worker.
   *
   * @returns `true` if worker was armed successfully, `false` if its status was `UCS_ERR_BUSY`.
   */
  bool arm();

  /**
   * @brief Progress worker event while in blocking progress mode.
   *
   * Blocks until a new worker event is happened and the worker notifies the file descriptor
   * associated to it. Requires blocking progress mode to be initialized with
   * `initBlockingProgressMode()` before the first call to this method.
   *
   * @code{.cpp}
   * // worker is `std::shared_ptr<ucxx::Worker>`
   *
   * // Block until UCX's wakes up for an incoming event, then fully progresses the
   * // worker
   * worker->initBlockingProgressMode();
   * worker->progressWorkerEvent();
   *
   * // All events have been progressed.
   * @endcode
   *
   * @throws std::ios_base::failure if creating any of the file descriptors or setting their
   *                                statuses.
   */
  bool progressWorkerEvent();

  /**
   * @brief Signal the worker that an event happened.
   *
   * Signals that an event has happened while, causing both either `progressWorkerEvent()`
   * or `waitProgress()` to immediately wake-up.
   *
   * @code{.cpp}
   * // worker is `std::shared_ptr<ucxx::Worker>`
   *
   * void progressThread() {
   *   // Block until UCX's wakes up for an incoming event, then fully progresses the
   *   // worker.
   *   worker->initBlockingProgressMode();
   *   worker->progressWorkerEvent();
   *
   *   // Will reach this point and exit after 3 seconds
   * }
   *
   * void otherThread() {
   *   // Signals the worker after 3 seconds
   *   std::this_thread::sleep_for(std::chrono::seconds(3));
   *   worker->signal();
   * }
   *
   * void mainThread() {
   *   t1 = std::thread(progressThread);
   *   t2 = std::thread(otherThread);
   *   t1.join();
   *   t2.join();
   * }
   * @endcode
   *
   * @throws ucxx::Error if an error occurred while attempting to signal the worker.
   */
  void signal();

  /**
   * @brief Block until an event has happened, then progresses.
   *
   * Blocks until an event has happened as part of UCX's wake-up mechanism.
   *
   * @code{.cpp}
   * // worker is `std::shared_ptr<ucxx::Worker>`
   *
   * // Block until UCX's wakes up for an incoming event, then fully progresses the
   * // worker
   * worker->waitProgress();
   * worker->progress();
   *
   * // All events have been progressed.
   * @endcode
   *
   * @throws ucxx::Error if an error occurred while attempting to arm the worker.
   *
   * @returns `true` if any communication was progressed, `false` otherwise.
   */
  bool waitProgress();

  /**
   * @brief Progress the worker only once.
   *
   * Wrapper for `ucp_worker_progress`.
   *
   * @code{.cpp}
   * // worker is `std::shared_ptr<ucxx::Worker>`
   * while (!worker->progressOnce()) ;
   *
   * // All events have been progressed.
   * @endcode
   *
   * @returns `true` if any communication was progressed, `false` otherwise.
   */
  bool progressOnce();

  /**
   * @brief Progress the worker until all communication events are completed.
   *
   * Iteratively calls `progressOnce()` until all communication events are completed.
   *
   * @code{.cpp}
   * // worker is `std::shared_ptr<ucxx::Worker>`
   * worker->progress();
   *
   * // All events have been progressed.
   * @endcode
   *
   * @returns `true`
   */
  bool progress();

  /**
   * @brief Register delayed submission.
   *
   * Register `ucxx::Request` for delayed submission. When the `ucxx::Worker` is created
   * with `enableDelayedSubmission=true`, calling actual UCX transfer routines will not
   * happen immediately and instead will be submitted later by the worker thread.
   *
   * The purpose of this method is to offload as much as possible any work to the worker
   * thread, thus decreasing computation on the caller thread, but potentially increasing
   * transfer latency.
   *
   * @param[in] callback the callback set to execute the UCP transfer routine during the
   *                     worker thread loop.
   */
  void registerDelayedSubmission(DelayedSubmissionCallbackType callback);

  /**
   * @brief Inquire if worker has been created with Python future supoprt.
   *
   * Check whether the worker has been created with Python future support. Reqiures
   * UCXX built with `-DUCXX_ENABLE_PYTHON=1`.
   *
   * @returns `true` if Python support is enabled, `false` otherwise.
   */
  bool isPythonFutureEnabled() const;

  /**
   * @brief Populate the Pyton future pool.
   *
   * To avoid taking the GIL for every new Python future required by each `ucxx::Request`,
   * the `ucxx::Worker` maintains a pool of futures that can be acquired when a new
   * `ucxx::Request` is created. Currently the pool has a maximum size of 100 objects,
   * and will refill once it goes under 50, otherwise calling this functions results in a
   * no-op.
   *
   * @throws std::runtime_error if UCXX was compiled without `-DUCXX_ENABLE_PYTHON=1` or if
   *                            `ucxx::Worker` was created with `enablePythonFuture=false`.
   */
  void populatePythonFuturesPool();

  /**
   * @brief Get a Python future from the pool.
   *
   * Get a Python future from the pool. If the pool is empty,
   * `ucxx::Worker::populatePythonFuturesPool()` is called and a warning is raised, since
   * that likely means the user is missing to call the aforementioned method regularly.
   *
   * @throws std::runtime_error if UCXX was compiled without `-DUCXX_ENABLE_PYTHON=1` or if
   *                            `ucxx::Worker` was created with `enablePythonFuture=false`.
   *
   * @returns The `shared_ptr<ucxx::python::Future>` object
   */
  std::shared_ptr<ucxx::python::Future> getPythonFuture();

  /**
   * @brief Block until a request event.
   *
   * Blocks until some communication is completed and Python future is ready to be notified,
   * shutdown was initiated or a timeout occurred (only if `periodNs > 0`). This method is
   * intended for use from Python, where a notifier thread will block until one of the
   * aforementioned events occur.
   *
   * @throws std::runtime_error if UCXX was compiled without `-DUCXX_ENABLE_PYTHON=1` or if
   *                            `ucxx::Worker` was created with `enablePythonFuture=false`.
   *
   * @returns `RequestNotifierWaitState::Ready` if some communication completed,
   *          `RequestNotifierWaitStats::Timeout` if a timeout occurred, or
   *          `RequestNotifierWaitStats::Shutdown` if shutdown has initiated.
   */
  python::RequestNotifierWaitState waitRequestNotifier(uint64_t periodNs);

  /**
   * @brief Notify Python futures of each completed communication request.
   *
   * Notifies Python futures of each completed communication request of their new status.
   * This method is intended to be used from Python, where a notifier thread will call
   * `waitRequestNotifier()` and block until some communication is completed, and then
   * call this method to notify all futures. The thread where this method is called from
   * must be using the same Python event loop as the thread that submitted the transfer
   * request.
   *
   * @throws std::runtime_error if UCXX was compiled without `-DUCXX_ENABLE_PYTHON=1` or if
   *                            `ucxx::Worker` was created with `enablePythonFuture=false`.
   */
  void runRequestNotifier();

  /**
   * @brief Signal the Python notifier thread to terminate.
   *
   * Signals the Python notifier thread to terminate, awakening the `waitRequestNotifier()`
   * blocking call.
   *
   * @throws std::runtime_error if UCXX was compiled without `-DUCXX_ENABLE_PYTHON=1` or if
   *                            `ucxx::Worker` was created with `enablePythonFuture=false`.
   */
  void stopRequestNotifierThread();

  /**
   * @brief Set callback to be executed at the progress thread start.
   *
   * Sets a callback that will be executed at the beginning of the progress thread. This
   * can be used to initialize any resources that are required to be available on the thread
   * the worker will be progressed from, such as a CUDA context.
   *
   * @param[in] callback function to execute during progress thread start
   * @param[in] callbackArg argument to be passed to the callback function
   */
  void setProgressThreadStartCallback(std::function<void(void*)> callback, void* callbackArg);

  /**
   * @brief Start the progress thread.
   *
   * Spawns a new thread that will take care of continuously progressing the worker. The
   * thread can progress the worker in blocking mode, using `progressWorkerEvent()` only
   * when worker events happen, or in polling mode by continuously calling `progress()`
   * (incurs in high CPU utilization).
   *
   * @param[in] pollingMode use polling mode if `true`, or blocking mode if `false`.
   * @param[in] callbackArg argument to be passed to the callback function
   */
  void startProgressThread(const bool pollingMode = false);

  /**
   * @brief Stop the progress thread.
   *
   * Stop the progress thread.
   *
   * May be called by the user at any time, and also called during destructor if the
   * worker thread was ever started.
   */
  void stopProgressThread();

  /**
   * @brief Cancel inflight requests.
   *
   * Cancel inflight requests, returning the total number of requests that were canceled.
   * This is usually executed during the progress loop.
   *
   * @returns Number of requests that were canceled.
   */
  size_t cancelInflightRequests();

  /**
   * @brief Schedule cancelation of inflight requests.
   *
   * Schedule inflight request to be canceled when `cancelInflightRequests()` is executed
   * the next time, usually during the progress loop. This is usually called from a
   * `ucxx::Endpoint`, for example when the error callback was called, signaling that
   * inflight requests for that endpoint will not be completed successfully and should be
   * canceled.
   *
   * @param[in] inflight requests object that implements the `cancelAll()` method.
   */
  void scheduleRequestCancel(std::shared_ptr<InflightRequests> inflightRequests);

  /**
   * @brief Remove reference to request from internal container.
   *
   * Remove the reference to a specific request from the internal container. This should
   * be called when a request has completed and the `ucxx::Worker` does not need to keep
   * track of it anymore. The raw pointer to a `ucxx::Request` is passed here as opposed
   * to the usual `std::shared_ptr<ucxx::Request>` used elsewhere, this is because the
   * raw pointer address is used as key to the requests reference, and this is called
   * from the object's destructor.
   *
   * @param[in] request raw pointer to the request
   */
  void removeInflightRequest(Request* request);

  /**
   * @brief Check for uncatched tag messages.
   *
   * Checks the worker for any uncatched tag messages. An uncatched tag message is any
   * tag message that has been fully or partially received by the worker, but not matched
   * by a corresponding `ucp_tag_recv_*` call.
   *
   * @code{.cpp}
   * // `worker` is `std::shared_ptr<ucxx::Worker>`
   * assert(!worker->tagProbe(0));
   *
   * // `ep` is a remote `std::shared_ptr<ucxx::Endpoint` to the local `worker`
   * ep->tagSend(buffer, length, 0);
   *
   * assert(worker->tagProbe(0));
   * @endcode
   *
   * @returns `true` if any uncatched messages were received, `false` otherwise.
   */
  bool tagProbe(ucp_tag_t tag);

  /**
   * @brief Enqueue a tag receive operation.
   *
   * Enqueue a tag receive operation, returning a `std::shared<ucxx::Request>` that can
   * be later awaited and checked for errors. This is a non-blocking operation, and the
   * status of the transfer must be verified from the resulting request object before the
   * data can be consumed.
   *
   * Using a Python future may be requested by specifying `enablePythonFuture`. If a
   * Python future is requested, the Python application must then await on this future to
   * ensure the transfer has completed. Requires UCXX to be compiled with
   * `UCXX_ENABLE_PYTHON=1`.
   *
   * @param[in] buffer              a raw pointer to pre-allocated memory where resulting
   *                                data will be stored.
   * @param[in] length              the size in bytes of the tag message to be received.
   * @param[in] tag                 the tag to match.
   * @param[in] enablePythonFuture  whether a python future should be created and
   *                                subsequently notified.
   * @param[in] callbackFunction    user-defined callback function to call upon completion.
   * @param[in] callbackData        user-defined data to pass to the `callbackFunction`.
   *
   * @returns Request to be subsequently checked for the completion and its state.
   */
  std::shared_ptr<Request> tagRecv(
    void* buffer,
    size_t length,
    ucp_tag_t tag,
    const bool enablePythonFuture                               = false,
    std::function<void(std::shared_ptr<void>)> callbackFunction = nullptr,
    std::shared_ptr<void> callbackData                          = nullptr);

  /**
   * @brief Get the address of the UCX worker object.
   *
   * Gets the address of the underlying UCX worker object, which can then be passed
   * to a remote worker, allowing creating a new endpoint to the local worker via
   * `ucxx::Worker::createEndpointFromWorkerAddress()`.
   *
   * @throws ucxx::Error if an error occurred while attempting to get the worker address.
   *
   * @returns The address of the local worker.
   */
  std::shared_ptr<Address> getAddress();

  /**
   * @brief Create endpoint to worker listening on specific IP and port.
   *
   * Creates an endpoint to a remote worker listening on a specific IP address and port.
   * The remote worker must have an active listener created with
   * `ucxx::Worker::createListener()`.
   *
   * @code{.cpp}
   * // `worker` is `std::shared_ptr<ucxx::Worker>`
   * // Create endpoint to worker listening on `10.10.10.10:12345`.
   * auto ep = worker->createEndpointFromHostname("10.10.10.10", 12345);
   * @endcode
   *
   * @throws std::invalid_argument if the IP address or hostname is invalid.
   * @throws std::bad_alloc if there was an error allocating space to handle the address.
   * @throws ucxx::Error if an error occurred while attempting to create the endpoint.
   *
   * @param[in] ipAddress string containing the IP address of the remote worker.
   * @param[in] port port number where the remote worker is listening at.
   * @param[in] endpointErrorHandling enable endpoint error handling if `true`,
   *                                  disable otherwise.
   *
   * @returns The `shared_ptr<ucxx::Endpoint>` object
   */
  std::shared_ptr<Endpoint> createEndpointFromHostname(std::string ipAddress,
                                                       uint16_t port,
                                                       bool endpointErrorHandling = true);

  /**
   * @brief Create endpoint to worker located at UCX address.
   *
   * Creates an endpoint to a listener-independent remote worker. The worker location is
   * identified by its UCX address, wrapped by a `std::shared_ptr<ucxx::Address>` object.
   *
   * @code{.cpp}
   * // `worker` is `std::shared_ptr<ucxx::Worker>`
   * auto localAddress = worker->getAddress();
   *
   * // pass address to remote process
   * // ...
   *
   * // receive address received from remote process
   * // ...
   *
   * // `remoteAddress` is `std::shared_ptr<ucxx::Address>`
   * auto ep = worker->createEndpointFromAddress(remoteAddress);
   * @endcode
   *
   * @throws ucxx::Error if an error occurred while attempting to create the endpoint.
   *
   * @param[in] address address of the remote UCX worker.
   * @param[in] endpointErrorHandling enable endpoint error handling if `true`,
   *                                  disable otherwise.
   *
   * @returns The `shared_ptr<ucxx::Endpoint>` object
   */
  std::shared_ptr<Endpoint> createEndpointFromWorkerAddress(std::shared_ptr<Address> address,
                                                            bool endpointErrorHandling = true);

  /**
   * @brief Listen for remote connections on given port.
   *
   * Starts a listener on given port. The listener allows remote processes to connect to
   * the local worker via an IP and port pair. The connection is then handle via a
   * callback specified by the user.
   *
   * @throws std::bad_alloc if there was an error allocating space to handle the address.
   * @throws ucxx::Error if an error occurred while attempting to create the listener or
   *                     to acquire its address.
   *
   * @param[in] port port number where to listen at.
   * @param[in] callback to handle each incoming connection.
   * @param[in] callback_args pointer to argument to pass to the callback.
   *
   * @returns The `shared_ptr<ucxx::Listener>` object
   */
  std::shared_ptr<Listener> createListener(uint16_t port,
                                           ucp_listener_conn_callback_t callback,
                                           void* callbackArgs);
};

}  // namespace ucxx
