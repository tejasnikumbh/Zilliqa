/**
* Copyright (c) 2018 Zilliqa 
* This source code is being disclosed to you solely for the purpose of your participation in 
* testing Zilliqa. You may view, compile and run the code for that purpose and pursuant to 
* the protocols and algorithms that are programmed into, and intended by, the code. You may 
* not do anything else with the code without express permission from Zilliqa Research Pte. Ltd., 
* including modifying or publishing the code (or any part of it), and developing or forming 
* another public or private blockchain network. This source code is provided ‘as is’ and no 
* warranties are given as to title or non-infringement, merchantability or fitness for purpose 
* and, to the extent permitted by law, all liability for your use of the code is disclaimed. 
* Some programs in this code are governed by the GNU General Public License v3.0 (available at 
* https://www.gnu.org/licenses/gpl-3.0.en.html) (‘GPLv3’). The programs that are governed by 
* GPLv3.0 are those programs that are located in the folders src/depends and tests/depends 
* and which include a reference to GPLv3 in their program files.
**/

#include <utility>

#include "TxBlock.h"
#include "libUtils/Logger.h"

using namespace std;
using namespace boost::multiprecision;

uint32_t TxBlock::SerializeIsMicroBlockEmpty() const
{
    int ret = 0;
    for (int i = m_isMicroBlockEmpty.size() - 1; i >= 0; --i)
    {
        ret = 2 * ret + m_isMicroBlockEmpty[i];
    }
    return ret;
}

unsigned int TxBlock::Serialize(vector<unsigned char>& dst,
                                unsigned int offset) const
{
    if (m_header.GetNumMicroBlockHashes() != m_microBlockHashes.size())
    {
        LOG_GENERAL(WARNING,
                    "assertion failed (" << __FILE__ << ":" << __LINE__ << ": "
                                         << __FUNCTION__ << ")");
    }

    unsigned int size_needed = GetSerializedSize();
    unsigned int size_remaining = dst.size() - offset;

    if (size_remaining < size_needed)
    {
        dst.resize(size_needed + offset);
    }

    m_header.Serialize(dst, offset);

    unsigned int curOffset = offset + TxBlockHeader::SIZE;

    SetNumber<uint32_t>(dst, curOffset, SerializeIsMicroBlockEmpty(),
                        sizeof(uint32_t));
    curOffset += sizeof(uint32_t);

    for (unsigned int i = 0; i < m_header.GetNumMicroBlockHashes(); i++)
    {
        const uint32_t& shardID = m_shardIDs[i];
        SetNumber<uint32_t>(dst, curOffset, shardID, sizeof(uint32_t));
        curOffset += sizeof(uint32_t);

        const TxnHash& microBlockTxHash = m_microBlockHashes.at(i).m_txRootHash;
        copy(microBlockTxHash.asArray().begin(),
             microBlockTxHash.asArray().end(), dst.begin() + curOffset);
        curOffset += TRAN_HASH_SIZE;

        const StateHash& microBlockDeltaHash
            = m_microBlockHashes.at(i).m_stateDeltaHash;
        copy(microBlockDeltaHash.asArray().begin(),
             microBlockDeltaHash.asArray().end(), dst.begin() + curOffset);
        curOffset += STATE_HASH_SIZE;
    }

    BlockBase::Serialize(dst, curOffset);

    return size_needed;
}

vector<bool> TxBlock::DeserializeIsMicroBlockEmpty(uint32_t arg)
{
    vector<bool> ret;
    for (uint i = 0; i < m_header.GetNumMicroBlockHashes(); ++i)
    {
        ret.push_back((bool)(arg % 2));
        arg /= 2;
    }
    return ret;
}

int TxBlock::Deserialize(const vector<unsigned char>& src, unsigned int offset)
{
    try
    {
        TxBlockHeader header;
        if (header.Deserialize(src, offset) != 0)
        {
            LOG_GENERAL(WARNING, "We failed to deserialize header.");
            return -1;
        }
        m_header = header;

        unsigned int curOffset = offset + TxBlockHeader::SIZE;

        m_isMicroBlockEmpty = DeserializeIsMicroBlockEmpty(
            GetNumber<uint32_t>(src, curOffset, sizeof(uint32_t)));
        curOffset += sizeof(uint32_t);

        for (unsigned int i = 0; i < m_header.GetNumMicroBlockHashes(); i++)
        {
            uint32_t shardID
                = GetNumber<uint32_t>(src, curOffset, sizeof(uint32_t));
            curOffset += sizeof(uint32_t);

            m_shardIDs.emplace_back(shardID);

            MicroBlockHashSet microBlockHash;

            copy(src.begin() + curOffset,
                 src.begin() + curOffset + TRAN_HASH_SIZE,
                 microBlockHash.m_txRootHash.asArray().begin());
            curOffset += TRAN_HASH_SIZE;

            copy(src.begin() + curOffset,
                 src.begin() + curOffset + TRAN_HASH_SIZE,
                 microBlockHash.m_stateDeltaHash.asArray().begin());
            curOffset += STATE_HASH_SIZE;

            m_microBlockHashes.push_back(microBlockHash);
        }
        if (m_header.GetNumMicroBlockHashes() != m_microBlockHashes.size())
        {
            LOG_GENERAL(WARNING,
                        "assertion failed (" << __FILE__ << ":" << __LINE__
                                             << ": " << __FUNCTION__ << ")");
        }

        BlockBase::Deserialize(src, curOffset);
    }
    catch (const std::exception& e)
    {
        LOG_GENERAL(WARNING,
                    "Error with TxBlock::Deserialize." << ' ' << e.what());
        return -1;
    }
    return 0;
}

unsigned int TxBlock::GetSerializedSize() const
{
    return TxBlockHeader::SIZE + sizeof(uint32_t)
        + (m_microBlockHashes.size()
           * (TRAN_HASH_SIZE + STATE_HASH_SIZE + sizeof(uint32_t)))
        + BlockBase::GetSerializedSize();
    ;
}

unsigned int TxBlock::GetMinSize() { return TxBlockHeader::SIZE; }

// creates a dummy invalid placeholder block -- blocknum is maxsize of uint256
TxBlock::TxBlock() {}

TxBlock::TxBlock(const vector<unsigned char>& src, unsigned int offset)
{
    if (Deserialize(src, offset) != 0)
    {
        LOG_GENERAL(WARNING, "We failed to init TxBlock.");
    }
}

TxBlock::TxBlock(TxBlockHeader&& header, vector<bool>&& isMicroBlockEmpty,
                 vector<MicroBlockHashSet>&& microBlockHashes,
                 vector<uint32_t>&& shardIDs, CoSignatures&& cosigs)
    : m_header(move(header))
    , m_isMicroBlockEmpty(move(isMicroBlockEmpty))
    , m_microBlockHashes(move(microBlockHashes))
    , m_shardIDs(move(shardIDs))
{
    if (m_header.GetNumMicroBlockHashes() != m_microBlockHashes.size()
        && m_header.GetNumMicroBlockHashes() != m_shardIDs.size())
    {
        LOG_GENERAL(WARNING,
                    "assertion failed (" << __FILE__ << ":" << __LINE__ << ": "
                                         << __FUNCTION__ << ")");
    }

    m_cosigs = move(cosigs);
}

const TxBlockHeader& TxBlock::GetHeader() const { return m_header; }

const std::vector<bool>& TxBlock::GetIsMicroBlockEmpty() const
{
    return m_isMicroBlockEmpty;
}

const vector<MicroBlockHashSet>& TxBlock::GetMicroBlockHashes() const
{
    return m_microBlockHashes;
}

const vector<uint32_t>& TxBlock::GetShardIDs() const { return m_shardIDs; }

bool TxBlock::operator==(const TxBlock& block) const
{
    return ((m_header == block.m_header)
            && (m_microBlockHashes == block.m_microBlockHashes));
}

bool TxBlock::operator<(const TxBlock& block) const
{
    if (m_header < block.m_header)
    {
        return true;
    }
    else if (m_header > block.m_header)
    {
        return false;
    }
    else if (m_microBlockHashes < block.m_microBlockHashes)
    {
        return true;
    }
    else if (m_microBlockHashes > block.m_microBlockHashes)
    {
        return false;
    }
    else
    {
        return false;
    }
}

bool TxBlock::operator>(const TxBlock& block) const
{
    return !((*this == block) || (*this < block));
}
