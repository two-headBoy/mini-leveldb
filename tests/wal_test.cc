#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "db/log_reader.h"
#include "db/log_writer.h"
#include "mini-leveldb/write_batch.h"

using namespace mini_leveldb;

namespace {

// 生成指定长度的可预测 payload
std::string MakePayload(size_t n, char seed = 'A') {
    std::string s;
    s.reserve(n);
    for (size_t i = 0; i < n; i++) {
        s.push_back(static_cast<char>(seed + (i % 26)));
    }
    return s;
}

// 用 tmpfile 打开，返回 FILE*（调用方负责 fclose）
std::FILE* NewTempFile() {
    return std::tmpfile();
}

}  // namespace

// 小记录往返：多条短记录
TEST(WalTest, SmallRecordsRoundTrip) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord("first record").ok());
        ASSERT_TRUE(w.AddRecord("second record").ok());
        ASSERT_TRUE(w.AddRecord("third record").ok());
    }

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "first record");
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "second record");
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "third record");
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// 空文件读取：直接 EOF
TEST(WalTest, EmptyFile) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    LogReader r(f);
    Slice rec;
    EXPECT_FALSE(r.ReadRecord(&rec));
    EXPECT_EQ(r.valid_end(), 0u);

    std::fclose(f);
}

// 大记录跨块分片（kFirstType → kLastType）
TEST(WalTest, LargeRecordSpansBlocks) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    const size_t kBigSize = 40 * 1024;  // 40KB > 32KB 块
    std::string big = MakePayload(kBigSize);

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord(big).ok());
    }

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    ASSERT_EQ(rec.size(), kBigSize);
    EXPECT_EQ(std::memcmp(rec.data(), big.data(), kBigSize), 0);
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// 超大记录多次分片（kFirstType → kMiddleType → kLastType）
TEST(WalTest, HugeRecordMultipleFragments) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    const size_t kHugeSize = 100 * 1024;  // 100KB，跨 3 个块
    std::string huge = MakePayload(kHugeSize, 'X');

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord(huge).ok());
    }

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    ASSERT_EQ(rec.size(), kHugeSize);
    EXPECT_EQ(std::memcmp(rec.data(), huge.data(), kHugeSize), 0);

    std::fclose(f);
}

// 多条大记录连续写，分片链不串
TEST(WalTest, MultipleLargeRecords) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    std::string r1 = MakePayload(20 * 1024, 'A');
    std::string r2 = MakePayload(50 * 1024, 'B');
    std::string r3 = MakePayload(10 * 1024, 'C');

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord(r1).ok());
        ASSERT_TRUE(w.AddRecord(r2).ok());
        ASSERT_TRUE(w.AddRecord(r3).ok());
    }

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    ASSERT_EQ(rec.ToString(), r1);
    ASSERT_TRUE(r.ReadRecord(&rec));
    ASSERT_EQ(rec.size(), r2.size());
    EXPECT_EQ(std::memcmp(rec.data(), r2.data(), r2.size()), 0);
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), r3);
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// 块尾填充：剩不足 7B 时填零，下一条记录从新块开始
TEST(WalTest, BlockTailPadding) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    // 写一条让块内剩余 < 7B 的记录
    // 块内可用 payload = 32768 - 7 = 32761
    // 写 32760 字节，剩 1 字节 < 7，触发填零
    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord(MakePayload(kBlockSize - kHeaderSize - 1, 'P')).ok());
        // 第二条应从新块开始
        ASSERT_TRUE(w.AddRecord("second").ok());
    }

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.size(), static_cast<size_t>(kBlockSize - kHeaderSize - 1));
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "second");

    std::fclose(f);
}

// CRC 损坏：篡改 payload 字节，Reader 应跳过坏块并在下一块重新同步
// LogReader 遇 kBad 会 pos_ = kBlockSize 跳过整块，所以好记录必须在下一块
TEST(WalTest, CorruptedPayloadRejected) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    // 块1: good first(17B) + corrupt me(18B) + 填充(32726+7) = 32768 满
    // 块2: good last
    const size_t kFillSize = kBlockSize - 17 - 18 - kHeaderSize;
    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord("good first").ok());
        ASSERT_TRUE(w.AddRecord("corrupt me").ok());
        ASSERT_TRUE(w.AddRecord(MakePayload(kFillSize, 'F')).ok());  // 填满块1
        ASSERT_TRUE(w.AddRecord("good last").ok());                 // 块2
    }

    // 定位第二条记录 payload 起点（17 + 7 = 24）并篡改
    std::fseek(f, 24, SEEK_SET);
    std::fputc('X', f);
    std::rewind(f);

    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "good first");
    // 坏记录被丢弃，跳过块1剩余，在块2重新同步读出 good last
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "good last");
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// CRC 损坏：篡改 CRC 字段本身，同样验证跨块重同步
TEST(WalTest, CorruptedCrcRejected) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    const size_t kFillSize = kBlockSize - 17 - 24 - kHeaderSize;
    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord("first good").ok());                 // 10B payload, 17B record
        ASSERT_TRUE(w.AddRecord("record to corrupt").ok());          // 17B payload, 24B record
        ASSERT_TRUE(w.AddRecord(MakePayload(kFillSize, 'F')).ok());  // 填满块1
        ASSERT_TRUE(w.AddRecord("next good").ok());                  // 块2
    }

    // 篡改第二条记录 CRC 第一个字节（偏移 17）
    std::fseek(f, 17, SEEK_SET);
    int c = std::fgetc(f);
    std::fseek(f, 17, SEEK_SET);
    std::fputc(c ^ 0xff, f);
    std::rewind(f);

    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "first good");
    ASSERT_TRUE(r.ReadRecord(&rec));  // 跳过坏块，读出块2的 next good
    EXPECT_EQ(rec.ToString(), "next good");
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// valid_end 追踪：恢复时截断到最后完好记录终点
TEST(WalTest, ValidEndTracking) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    const std::string r1 = "first";
    const std::string r2 = "second record";

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord(r1).ok());
        ASSERT_TRUE(w.AddRecord(r2).ok());
    }

    // 在文件尾部追加一段脏数据（模拟崩溃残尾）
    std::fseek(f, 0, SEEK_END);
    std::fputs("\x99\x99\x99\x99\x99\x99\x99garbage", f);

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), r1);
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), r2);
    EXPECT_FALSE(r.ReadRecord(&rec));

    // valid_end 应等于两条完好记录的总字节数
    // r1: 7 + 5 = 12
    // r2: 7 + 13 = 20
    EXPECT_EQ(r.valid_end(), 12u + 20u);

    std::fclose(f);
}

// LogWriter offset 续写：模拟恢复后从已有文件大小继续写
TEST(WalTest, WriterOffsetResume) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    // 第一阶段：写一些记录
    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord("phase1-a").ok());
        ASSERT_TRUE(w.AddRecord("phase1-b").ok());
    }

    // 获取当前文件大小
    std::fseek(f, 0, SEEK_END);
    const long phase1_size = std::ftell(f);

    // 第二阶段：用 offset 续写（模拟 DBImpl::Init 里的恢复场景）
    {
        LogWriter w(f, static_cast<uint64_t>(phase1_size));
        ASSERT_TRUE(w.AddRecord("phase2-a").ok());
        ASSERT_TRUE(w.AddRecord("phase2-b").ok());
    }

    // 读全部验证
    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "phase1-a");
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "phase1-b");
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "phase2-a");
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "phase2-b");
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// LogWriter offset 续写跨块边界：phase1 正好写到块尾附近
TEST(WalTest, WriterOffsetResumeAcrossBlock) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    // 写到块内偏移接近末尾
    {
        LogWriter w(f);
        // payload 32760，加 7B header = 32767，剩 1 字节触发填零
        ASSERT_TRUE(w.AddRecord(MakePayload(kBlockSize - kHeaderSize - 1, 'Z')).ok());
    }

    std::fseek(f, 0, SEEK_END);
    const long end = std::ftell(f);

    // 续写应从新块开始
    {
        LogWriter w(f, static_cast<uint64_t>(end));
        ASSERT_TRUE(w.AddRecord("after-resume").ok());
    }

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.size(), static_cast<size_t>(kBlockSize - kHeaderSize - 1));
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "after-resume");

    std::fclose(f);
}

// WAL + WriteBatch 集成：WriteBatch.Data() 直接落 WAL 再回放
TEST(WalTest, WriteBatchRoundTripThroughWal) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    WriteBatch batch;
    batch.set_sequence(100);
    batch.Put("alice", "100");
    batch.Delete("bob");
    batch.Put("charlie", "300");

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord(batch.Data()).ok());
    }

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));

    WriteBatch restored;
    ASSERT_TRUE(restored.SetContents(rec).ok());
    EXPECT_EQ(restored.Count(), 3);

    // 直接验证 seq 起始值
    EXPECT_EQ(restored.sequence(), 100u);

    std::fclose(f);
}

// 空 payload：len=0，EmitPhysicalRecord 的 (len>0) 分支跳过 fwrite
TEST(WalTest, EmptyPayload) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord("").ok());
        ASSERT_TRUE(w.AddRecord("non-empty").ok());
        ASSERT_TRUE(w.AddRecord("").ok());
    }

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_TRUE(rec.empty());
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "non-empty");
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_TRUE(rec.empty());
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// 恰好装满一块：payload = 32761B，record = 32768B，不触发填零
TEST(WalTest, ExactlyFillOneBlock) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    const size_t kExactSize = kBlockSize - kHeaderSize;  // 32761
    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord(MakePayload(kExactSize, 'E')).ok());
        // 第二条应从新块开始（块1已无空间）
        ASSERT_TRUE(w.AddRecord("second").ok());
    }

    // 验证文件大小 = 32768 + (7+6) = 32781，无填零
    std::fseek(f, 0, SEEK_END);
    EXPECT_EQ(std::ftell(f), static_cast<long>(kBlockSize + kHeaderSize + 6));

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.size(), kExactSize);
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "second");

    std::fclose(f);
}

// 孤儿 FIRST 丢弃：模拟崩溃只写了大记录的第一片，Reader 应丢弃半条
TEST(WalTest, OrphanFirstFragmentDiscarded) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    // 写一条小记录，然后写一条大记录（会跨块产生 FIRST 片），
    // 但在 LAST 片之前截断文件模拟崩溃
    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord("survives").ok());
        // 50KB 会跨块，产生 FIRST + LAST 两片
        ASSERT_TRUE(w.AddRecord(MakePayload(50 * 1024, 'C')).ok());
    }

    // 截断到只保留 "survives" + FIRST 片的一部分
    // "survives" = 9B payload, record = 16B
    // FIRST 片 = header(7) + payload(32745) = 32752B
    // 截断到 16 + 100（保留 FIRST 头 + 部分 payload），模拟崩溃
    std::fseek(f, 16 + 100, SEEK_SET);
    std::fflush(f);
    ::ftruncate(::fileno(f), 16 + 100);

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "survives");
    // FIRST 片残缺，Reader 读到 EOF 应返回 false（丢弃半条）
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// 截断 mid-header：文件只有 3B header，连 7B 头都不完整
TEST(WalTest, TruncatedMidHeader) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord("complete").ok());
    }

    // 追加 3 字节残缺 header
    std::fseek(f, 0, SEEK_END);
    std::fputs("\x01\x02\x03", f);

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "complete");
    // 残缺 header 应被视为 EOF
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}

// 截断 mid-payload：header 完整但 payload 不完整
TEST(WalTest, TruncatedMidPayload) {
    std::FILE* f = NewTempFile();
    ASSERT_NE(f, nullptr);

    {
        LogWriter w(f);
        ASSERT_TRUE(w.AddRecord("complete").ok());
    }

    // 构造一条 header 声称 payload 100B 但实际只有 5B 的残缺记录
    std::fseek(f, 0, SEEK_END);
    char header[7] = {0};
    header[4] = 100;  // len low byte
    header[5] = 0;    // len high byte
    header[6] = kFullType;
    std::fwrite(header, 1, 7, f);
    std::fwrite("hello", 1, 5, f);  // 只写 5B payload，声称 100B

    std::rewind(f);
    LogReader r(f);
    Slice rec;
    ASSERT_TRUE(r.ReadRecord(&rec));
    EXPECT_EQ(rec.ToString(), "complete");
    // payload 越界 + eof，应返回 false
    EXPECT_FALSE(r.ReadRecord(&rec));

    std::fclose(f);
}
