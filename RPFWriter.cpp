#include <iostream>
#include <fstream>
#include <Windows.h>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <numeric>
#include "rpftypes.h"
#include "blockwriter.h"

std::vector<std::filesystem::path> readDirectory(const std::filesystem::path& path, bool root = true)
{
    std::vector<std::filesystem::path> structure;
    std::vector<std::filesystem::path> directories;

    structure.push_back(path);

    for (const auto& entry : std::filesystem::directory_iterator(path))
    {
        structure.push_back(entry);

        if (entry.path().extension().string() == ".rpf")
            continue;

        if (entry.is_directory())
            directories.push_back(entry);
    }

    for (auto dir : directories)
    {
        auto str = readDirectory(dir, false);
        structure.reserve(structure.size() + str.size());
        structure.insert(structure.end(), str.begin() + 1, str.end());
    }
    return structure;
}

class Packfile
{
    Header7 header;

    uint32_t dataCount;
    std::unordered_map<uint32_t, std::string> nameTable;

    uint32_t fileCount;
    std::unordered_map<uint32_t, BlockWriter*> data;

    uint32_t currentDirIndex;
    std::unordered_map<uint32_t, std::vector<Entry>> entries;

    size_t nameTableSize;
    size_t dataBlockSize;

public:
    Packfile() : dataCount(0), nameTableSize(0), dataBlockSize(0), fileCount(0)
    {
        header.magic = 0x52504637;
        header.encryption = 0x4E45504F;
    }

    ~Packfile()
    {
        for (auto& d : data)
        {
            delete d.second;
        }
    }

    size_t headerBlockSize()
    {
        return ceil((double)(sizeof(Header7) + (sizeof(Entry) * dataCount) + nameTableSize) / 512) * 512;
    }

    inline size_t getDataBlockSize()
    {
        return dataBlockSize;
    }

    size_t nextBlockIndex()
    {
        return (headerBlockSize() + dataBlockSize) / 512;
    }

    void addName(const std::string& name)
    {
        nameTable[dataCount] = name;
        nameTableSize += name.size() + 1;
    }

    void addData(BlockWriter* fileData, size_t len)
    {
        data[++fileCount] = fileData;

        dataBlockSize += (uint32_t)ceil((double)len / 512) * 512;
    }

    void addDirectory(const std::string& name, uint32_t index, uint32_t count)
    {
        currentDirIndex = index;
        printf("Adding dir %s, idx %d, children %d\n", name.c_str(), index, count);
        addName(name);

        Entry entry{ 0 };
        entry.dir.entryType = 0x7FFFFF00;
        entry.dir.nameOffset = (uint32_t)(nameTableSize - name.size() - 1);
        entry.dir.entryIndex = index;
        entry.dir.entriesCnt = count;

        entries[currentDirIndex].push_back(entry);

        dataCount++;
    }

    void addBinary(const std::string& name, BlockWriter* fileData, size_t len)
    {
        printf("Adding binary %d: %s %d\n", fileCount, name.c_str(), nextBlockIndex());

        addName(name);

        Entry entry{ 0 };
        entry.bin.fileOffset[0] = nextBlockIndex() & 0xFF;
        entry.bin.fileOffset[1] = (nextBlockIndex() >> 8) & 0xFF;
        entry.bin.fileOffset[2] = (nextBlockIndex() >> 16) & 0xFF;
        entry.bin.nameOffset = (uint32_t)(nameTableSize - name.size() - 1);
        entry.bin.realSize = len;
        entry.bin.isEncrypted = 0;

        entries[currentDirIndex].push_back(entry);

        addData(fileData, len);

        dataCount++;
    }

    void addResource(const std::string& name, BlockWriter* fileData, size_t len, uint32_t sysFlags, uint32_t gfxFlags)
    {
        printf("Adding resource %d: %s %d %d %d\n", fileCount, name.c_str(), nextBlockIndex(), sysFlags, gfxFlags);

        addName(name);

        Entry entry{ 0 };
        entry.res.fileOffset[0] = nextBlockIndex() & 0xFF;
        entry.res.fileOffset[1] = (nextBlockIndex() >> 8) & 0xFF;
        entry.res.fileOffset[2] = (nextBlockIndex() >> 16) & 0xFF;
        entry.res.nameOffset = (uint16_t)(nameTableSize - name.size() - 1);
        entry.res.systemFlags = sysFlags;
        entry.res.graphicsFlags = gfxFlags;
        entry.res.fileSize[0] = len & 0xFF;
        entry.res.fileSize[1] = (len >> 8) & 0xFF;
        entry.res.fileSize[2] = (len >> 16) & 0xFF;

        entries[currentDirIndex].push_back(entry);

        addData(fileData, len);

        dataCount++;
    }

    void reorderEntries()
    {
        std::unordered_map<uint32_t, Entry> newEntries;
    }

    BlockWriter* serialize()
    {
        auto writer = new BlockWriter();

        header.entryCount = dataCount;
        header.nameLength = ceil((double)nameTableSize / 16) * 16;

        printf("Creating rpf. Entries: %d, namelen: %d\n", dataCount, nameTableSize);

        writer->write(header);

        for (auto& ent : entries)
        {
            printf("entry %d\n", ent.first);
            for (auto entry : ent.second)
            {
                writer->write(entry);
            }
        }

        for (int i = 0; i < dataCount; i++)
        {
            writer->write(*nameTable[i].c_str(), nameTable[i].size() + 1);
        }

        for (auto& d : data)
        {
            for (int i = 0; i <= d.second->blocks; i++)
            {
                writer->writeNextBlock(d.second->block[i]->buffer, 512);
            }
        }

        return writer;
    }
};

uint32_t filesInDirectory(const std::filesystem::path& path)
{
    uint32_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(path))
        files++;
    return files;
}

Packfile* readRPF(const std::string& rpfpath)
{
    auto rpf = new Packfile();

    uint32_t idx = 1;
    auto structure = readDirectory(rpfpath);

    for (auto path : structure)
    {
        if (std::filesystem::is_directory(path))
        {
            //printf("Path: %s | ext: %s\n", rpfpath.c_str(), path.filename().extension().string().c_str());
            if (path.filename().extension().string() == ".rpf" && path.string() != rpfpath)
            {
                auto subrpf = readRPF(path.string());
                rpf->addBinary(path.filename().string(), subrpf->serialize(), subrpf->headerBlockSize() + subrpf->getDataBlockSize());
            }
            else
            {
                auto numFiles = filesInDirectory(path);
                rpf->addDirectory(idx == 1 ? "" : path.filename().string(), idx, filesInDirectory(path));
                idx += numFiles;
            }
        }
        else
        {
            auto writer = new BlockWriter();
            std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);

            std::streampos size = file.tellg();
            file.seekg(0, std::ios::beg);

            uint32_t writingSize = 0;
            while (file)
            {
                auto memblock = new char[USHRT_MAX];
                file.read(memblock, USHRT_MAX);

                size_t readSize = USHRT_MAX;
                if (!file)
                    readSize = file.gcount();
                
                writer->write(*memblock, readSize);

                writingSize += readSize;

                //printf("reading %d of %u\n", writingSize, size);
            }

            file.close();

            auto header = writer->get<uint32_t>();
            //printf("Adding %s\n", path.filename().string().c_str());

            if (header == 0x37435352)
                rpf->addResource(path.filename().string(), writer, size, writer->get<uint32_t>(0, 8), writer->get<uint32_t>(0, 12));
            else
                rpf->addBinary(path.filename().string(), writer, size);
        }
    }

    return rpf;
}


int main()
{
    auto writer = readRPF("rpftests/in.rpf")->serialize();

    std::ofstream file("rpftests/out.rpf", std::ios::out | std::ios::trunc | std::ios::binary);

    for (size_t i = 0; i <= writer->blocks; i++)
    {
        file.write(writer->block[i]->buffer, 512);
    }

    file.close();
    return getchar();
}