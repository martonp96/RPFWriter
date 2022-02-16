#pragma once

class BlockWriter
{
    struct Block
    {
        size_t pos;
        char buffer[512];

        Block() : pos(0), buffer{ 0 } {}

        template<typename T>
        void write(const T& data, size_t size = sizeof(T))
        {
            memcpy(&buffer[pos], &data, size);
            pos += size;
        }

        inline size_t space()
        {
            return 512 - pos;
        }
    };
public:
    size_t blocks = 0;
    std::unordered_map<size_t, Block*> block;

    template<typename T>
    void write(const T& data, size_t size = sizeof(T))
    {
        if (!block[blocks])
            block[blocks] = new Block();

        auto space = block[blocks]->space();
        if (space == 0)
        {
            writeNextBlock(data, size);
        }
        else if (size > space)
        {
            block[blocks]->write(data, space);
            write(((char*)&data)[space], size - space);
        }
        else
        {
            block[blocks]->write(data, size);
        }
    }

    template<typename T>
    void writeNextBlock(const T& data, size_t size = sizeof(T))
    {
        blocks++;
        write(data, size);
    }

    template<typename T>
    T get(int blockIdx = 0, int pos = 0)
    {
        return *(T*)&block[blockIdx]->buffer[pos];
    }

    size_t size()
    {
        return (blocks * 512) + block[blocks - 1]->pos;
    }
};
