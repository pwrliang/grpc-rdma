#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <sstream>
#include <thread>
#include <mpi.h>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <grpc/impl/codegen/log.h>
#include "benchmark_utils.h"

#ifdef BAZEL_BUILD
#include "examples/protos/benchmark.grpc.pb.h"
#else

#include "benchmark.grpc.pb.h"

#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using benchmark::BENCHMARK;
using benchmark::Data_Empty;
using benchmark::Data_Bytes;
using benchmark::Data_String;
using benchmark::Data_Int64;
using benchmark::Complex;

int world_size, world_rank;

void MPI_summary(int64_t time, const char *prefix) {
    MPI_Barrier(MPI_COMM_WORLD);
    if (world_rank != 0) {
        MPI_Send(&time, 1, MPI_INT64_T, 0, 0, MPI_COMM_WORLD);
        return;
    }
    int64_t total_time = time, _time_;
    for (int i = 1; i < world_size; i++) {
        MPI_Recv(&_time_, 1, MPI_INT64_T, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        total_time += _time_;
    }
    printf("%s: world size = %d, total time duration = %lld ms, average time duration = %lld ms\n",
           prefix, world_size, total_time, total_time / world_size);
    // printf("%s: time duration = %lld ms\n",
    //         prefix, time);
}

class BenchmarkClient {
public:
    BenchmarkClient(std::shared_ptr<Channel> channel) : stub_(BENCHMARK::NewStub(channel)) {}

    void SyncSayHello() {
        ClientContext context;
        Data_Empty request, reply;
        Status status = stub_->SayHello(&context, request, &reply);
        if (!status.ok()) {
            printf("SyncSayHello failed: not ok\n");
            abort();
        } else {
            printf("SyncSayHello succeed\n");
        }
    }

    void SyncBiUnary(size_t batch_size, size_t _request_size_, size_t _reply_size_) {
        Complex request;
        size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
        size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
        size_t request_size, reply_size;
        for (size_t i = 0; i < batch_size; i++) {
            ClientContext context;
            Complex reply;
            request_size = random(min_request_size, max_request_size);
            reply_size = random(min_reply_size, max_reply_size);
            request.mutable_datas()->mutable_data1()->resize(request_size);
            request.mutable_numbers()->set_number1(reply_size);
            if (!stub_->BiUnary(&context, request, &reply).ok()) {
                printf("SyncBiUnary failed: not ok\n");
                abort();
            }
            if (reply.datas().data1().length() != reply_size) {
                printf("SyncBiUnary failed: actual reply size != expected reply size\n");
                abort();
            }
            // printf("SyncBiUnary succeed\n");
        }
    }

    void SyncClientStream(size_t batch_size, size_t _request_size_) {
        ClientContext context;
        Complex request, reply;
        size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
        size_t request_size;
        size_t total_request_size = 0;
        std::unique_ptr<ClientWriter<Complex>> writer(stub_->ClientStream(&context, &reply));
        for (size_t i = 0; i < batch_size; i++) {
            request_size = random(min_request_size, max_request_size);
            request.mutable_datas()->mutable_data1()->resize(request_size);
            if (!writer->Write(request)) {
                printf("SyncClientStream failed: the stream has been closed\n");
                abort();
            }
            total_request_size += request_size;
        }
        writer->WritesDone();
        if (!writer->Finish().ok()) {
            printf("SyncClientStream failed: no ok after write done\n");
            abort();
        }
        if (reply.numbers().number1() != total_request_size) {
            printf("SyncClientStream failed: actual reply size != expected reply size\n");
            abort();
        }
    }

    void SyncServerStream(size_t batch_size, size_t _reply_size_) {
        ClientContext context;
        Complex request, reply;
        size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
        size_t actual_batch_size = 0;
        size_t total_reply_size;
        request.mutable_numbers()->set_number1(batch_size);
        request.mutable_numbers()->set_number2(min_reply_size);
        request.mutable_numbers()->set_number3(max_reply_size);
        std::unique_ptr<ClientReader<Complex>> reader(stub_->ServerStream(&context, request));
        while (reader->Read(&reply)) {
            size_t actual_min_reply = reply.numbers().number1();
            size_t actual_max_reply = reply.numbers().number2();
            if (actual_min_reply != min_reply_size || actual_max_reply != max_reply_size) {
                printf("SyncServerStream failed, actual reply size != expected reply size\n");
                abort();
            }
            actual_batch_size++;
        }
        if (!reader->Finish().ok()) {
            printf("SyncServerStream failed, not ok\n");
            abort();
        }
    }

    void SyncBiStream(size_t batch_size, size_t _request_size_, size_t _reply_size_) {
        ClientContext context;
        size_t min_request_size = _request_size_ / 2, max_request_size = _request_size_ * 2;
        size_t min_reply_size = _reply_size_ / 2, max_reply_size = _reply_size_ * 2;
        size_t request_size, reply_size;
        size_t expected_total_request_size = 0, expected_total_reply_size = 0;

        std::shared_ptr<ClientReaderWriter<Complex, Complex>> stream(stub_->BiStream(&context));

        std::thread writer([&]() {
            Complex request;
            for (int i = 0; i < batch_size; i++) {
                request_size = random(min_request_size, max_request_size);
                reply_size = random(min_reply_size, max_reply_size);
                request.mutable_datas()->mutable_data1()->resize(request_size);
                request.mutable_numbers()->set_number1(reply_size);
                stream->Write(request);
                expected_total_request_size += request_size;
                expected_total_reply_size += reply_size;
            }
            stream->WritesDone();
        });

        Complex reply;
        size_t actual_total_request_size = 0, actual_total_reply_size = 0, actual_batch_size = 0;
        while (stream->Read(&reply)) {
            actual_batch_size++;
            actual_total_reply_size += reply.datas().data1().length();
            actual_total_request_size += reply.numbers().number1();
        }

        writer.join();
        if (!stream->Finish().ok()) {
            printf("SyncBiStream failed: not ok\n");
            abort();
        }
        if (actual_batch_size != batch_size ||
            actual_total_request_size != expected_total_request_size ||
            actual_total_reply_size != expected_total_reply_size) {
            printf("SyncBiStream failed: actual reply size != expected reply size\n");
            abort();
        }
    }

    void BatchOperations(const size_t batch_size, const size_t data_size) {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::stringstream ss("SyncBiUnary, ");

        SyncBiUnary(batch_size, data_size / 2, data_size * 2);
        auto t1 = std::chrono::high_resolution_clock::now();
        size_t ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        ss.str("");
        ss << "SyncBiUnary, batch size = " << batch_size << ", data size = " << data_size;
        MPI_summary(ms, ss.str().c_str());

        SyncClientStream(batch_size, data_size);
        auto t2 = std::chrono::high_resolution_clock::now();
        ms = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        ss.str("");
        ss << "SyncClientStream, batch size = " << batch_size << ", data size = " << data_size;
        MPI_summary(ms, ss.str().c_str());

        SyncServerStream(batch_size, data_size);
        auto t3 = std::chrono::high_resolution_clock::now();
        ms = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
        ss.str("");
        ss << "SyncServerStream, batch size = " << batch_size << ", data size = " << data_size;
        MPI_summary(ms, ss.str().c_str());

        SyncBiStream(batch_size, data_size / 2, data_size * 2);
        auto t4 = std::chrono::high_resolution_clock::now();
        ms = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();
        ss.str("");
        ss << "SyncBiStream, batch size = " << batch_size << ", data size = " << data_size;
        MPI_summary(ms, ss.str().c_str());
    }

    void Test() {
        size_t max_size = 4ul * 1024 * 1024;
        std::atomic_bool updated;
        updated.store(false);
        std::thread th_monitor([&]() {
            while (true) {
                sleep(2);
                if (updated.load()) {
                    updated.store(false);
                } else {
                    printf("Stuck\n");
                    exit(1);
                }
            }
        });
        th_monitor.detach();

        while (true) {
            ClientContext ctx;
            benchmark::TestReq req;
            benchmark::TestResp resp;
            auto size = random(1, max_size);
            std::string &s = *req.mutable_data();
            s.resize(size);

            for (int i = 0; i < size; i++) {
                s[i] = (char) random(0, 255);
            }

            ctx.set_wait_for_ready(true);
            GPR_ASSERT(stub_->Test(&ctx, req, &resp).ok());
            GPR_ASSERT(req.data().size() == resp.data().size());
            GPR_ASSERT(req.data() == resp.data());
            updated.store(true);
        }
    }

private:
    std::unique_ptr<BENCHMARK::Stub> stub_;
};

DEFINE_string(server_address, "localhost:50051", "");
DEFINE_bool(sync_enable, true, "");
DEFINE_bool(async_enable, false, "");
DEFINE_string(platform, "TCP", "which transport protocol used");
DEFINE_string(verbosity, "ERROR", "");
DEFINE_string(data_sizes, "1024*1024,4*1024*1024", "");
DEFINE_string(batch_sizes, "1000,10000,20000,50000,100000", "");

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    ::gflags::ParseCommandLineFlags(&argc, &argv, true);
    ::gflags::ShutDownCommandLineFlags();

    const std::string server_address = FLAGS_server_address;
    const std::string platform = FLAGS_platform;
    const std::string verbosity = FLAGS_verbosity;
    const std::string _data_sizes_ = FLAGS_data_sizes;
    const std::string _batch_size_ = FLAGS_batch_sizes;

    std::vector<int> data_sizes = Split2Int(_data_sizes_, ',');
    std::vector<int> batch_sizes = Split2Int(_batch_size_, ',');

    setenv("GRPC_PLATFORM_TYPE", platform.c_str(), 1);
    setenv("RDMA_VERBOSITY", verbosity.c_str(), 1);

    BenchmarkClient client(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

    client.Test();
//    client.SyncSayHello();
//
//    for (int data_size: data_sizes) {
//        for (int batch_size: batch_sizes) {
//            printf("\n");
//            client.BatchOperations(batch_size, data_size);
//        }
//    }

    return 0;
}