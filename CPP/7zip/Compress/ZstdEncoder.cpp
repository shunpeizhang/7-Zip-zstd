// (C) 2016 - 2018 Tino Reichardt

#define DEBUG 0

#if DEBUG
#include <stdio.h>
#endif

#include "StdAfx.h"
#include "ZstdEncoder.h"
#include "ZstdDecoder.h"

#ifndef EXTRACT_ONLY
namespace NCompress {
namespace NZSTD {

CEncoder::CEncoder():
  _ctx(NULL),
  _srcBuf(NULL),
  _dstBuf(NULL),
  _srcBufSize(ZSTD_CStreamInSize()),
  _dstBufSize(ZSTD_CStreamOutSize()),
  _processedIn(0),
  _processedOut(0),
  _numThreads(NWindows::NSystem::GetNumberOfProcessors()),
  _Long(-1),
  _Strategy(-1),
  _WindowLog(-1),
  _HashLog(-1),
  _ChainLog(-1),
  _SearchLog(-1),
  _SearchLength(-1),
  _TargetLen(-1),
  _OverlapLog(-1),
  _LdmHashLog(-1),
  _LdmSearchLength(-1),
  _LdmBucketSizeLog(-1),
  _LdmHashEveryLog(-1)
{
  _props.clear();
  _hMutex = CreateMutex(NULL, FALSE, NULL);
}

CEncoder::~CEncoder()
{
  if (_ctx) {
    ZSTD_freeCCtx(_ctx);
    MyFree(_srcBuf);
    MyFree(_dstBuf);
    CloseHandle(_hMutex);
  }
}

STDMETHODIMP CEncoder::SetCoderProperties(const PROPID * propIDs, const PROPVARIANT * coderProps, UInt32 numProps)
{
  _props.clear();

  for (UInt32 i = 0; i < numProps; i++)
  {
    const PROPVARIANT & prop = coderProps[i];
    PROPID propID = propIDs[i];
    UInt32 v = (UInt32)prop.ulVal;
    switch (propID)
    {
    case NCoderPropID::kLevel:
      {
        if (prop.vt != VT_UI4)
          return E_INVALIDARG;

        /* level 1..22 */
        _props._level = static_cast < Byte > (prop.ulVal);
        Byte mylevel = static_cast < Byte > (ZSTD_LEVEL_MAX);
        if (_props._level > mylevel)
          _props._level = mylevel;

        break;
      }
    case NCoderPropID::kNumThreads:
      {
        SetNumberOfThreads(v);
        break;
      }

    case NCoderPropID::kStrategy:
      {
        if (v < 1) v = 1;
        if (v > 8) v = 8;
        _Strategy = v;
        break;
      }
    case NCoderPropID::kWindowLog:
      {
        if (v < ZSTD_WINDOWLOG_MIN) v = ZSTD_WINDOWLOG_MIN;
        if (v > ZSTD_WINDOWLOG_MAX) v = ZSTD_WINDOWLOG_MAX;
        _WindowLog = v;
        break;
      }
    case NCoderPropID::kHashLog:
      {
        if (v < ZSTD_HASHLOG_MIN) v = ZSTD_HASHLOG_MIN;
        if (v > ZSTD_HASHLOG_MAX) v = ZSTD_HASHLOG_MAX;
        _HashLog = v;
        break;
      }
    case NCoderPropID::kChainLog:
      {
        if (v < ZSTD_CHAINLOG_MIN) v = ZSTD_CHAINLOG_MIN;
        if (v > ZSTD_CHAINLOG_MAX) v = ZSTD_CHAINLOG_MAX;
        _ChainLog = v;
        break;
      }
    case NCoderPropID::kSearchLog:
      {
        if (v < ZSTD_SEARCHLOG_MIN) v = ZSTD_SEARCHLOG_MIN;
        if (v > ZSTD_SEARCHLOG_MAX) v = ZSTD_SEARCHLOG_MAX;
        _SearchLog = v;
        break;
      }
    case NCoderPropID::kSearchLength:
      {
        if (v < ZSTD_SEARCHLENGTH_MIN) v = ZSTD_SEARCHLENGTH_MIN;
        if (v > ZSTD_SEARCHLENGTH_MAX) v = ZSTD_SEARCHLENGTH_MAX;
        _SearchLength = v;
        break;
      }
    case NCoderPropID::kTargetLen:
      {
        if (v < ZSTD_TARGETLENGTH_MIN) v = ZSTD_TARGETLENGTH_MIN;
        if (v > ZSTD_TARGETLENGTH_MAX) v = ZSTD_TARGETLENGTH_MAX;
        _TargetLen = 0;
        break;
      }
    case NCoderPropID::kOverlapLog:
      {
        if (v < 0) v = 0; /* no overlap */
        if (v > 9) v = 9; /* full size */
        _OverlapLog = v;
        break;
      }
    case NCoderPropID::kLong:
      {
        /* exact like --long in zstd cli program */
        _Long = 1;
        if (v == 0) {
          // m0=zstd:long:tlen=x
          _WindowLog = 27;
        } else if (v < 10) {
          _WindowLog = 10;
        } else if (v > ZSTD_WINDOWLOG_MAX) {
          _WindowLog = ZSTD_WINDOWLOG_MAX;
        }
        break;
      }
    case NCoderPropID::kLdmHashLog:
      {
        if (v < ZSTD_HASHLOG_MIN) v = ZSTD_HASHLOG_MIN;
        if (v > ZSTD_HASHLOG_MAX) v = ZSTD_HASHLOG_MAX;
        _LdmHashLog = v;
        break;
      }
    case NCoderPropID::kLdmSearchLength:
      {
        if (v < ZSTD_LDM_MINMATCH_MIN) v = ZSTD_LDM_MINMATCH_MIN;
        if (v > ZSTD_LDM_MINMATCH_MAX) v = ZSTD_LDM_MINMATCH_MAX;
        _LdmSearchLength = v;
        break;
      }
    case NCoderPropID::kLdmBucketSizeLog:
      {
        if (v < 1) v = 1;
        if (v > ZSTD_LDM_BUCKETSIZELOG_MAX) v = ZSTD_LDM_BUCKETSIZELOG_MAX;
        _LdmBucketSizeLog = v;
        break;
      }
    case NCoderPropID::kLdmHashEveryLog:
      {
        if (v < 0) v = 0; /* 0 => automatic mode */
        if (v > (ZSTD_WINDOWLOG_MAX - ZSTD_HASHLOG_MIN)) v = (ZSTD_WINDOWLOG_MAX - ZSTD_HASHLOG_MIN);
        _LdmHashEveryLog = v;
        break;
      }
    default:
      {
        break;
      }
    }
  }

  return S_OK;
}

STDMETHODIMP CEncoder::WriteCoderProperties(ISequentialOutStream * outStream)
{
  return WriteStream(outStream, &_props, sizeof (_props));
}

STDMETHODIMP CEncoder::Code(ISequentialInStream *inStream,
  ISequentialOutStream *outStream, const UInt64 * /*inSize*/ ,
  const UInt64 * /*outSize */, ICompressProgressInfo *progress)
{
  ZSTD_EndDirective ZSTD_todo = ZSTD_e_continue;
  ZSTD_outBuffer outBuff;
  ZSTD_inBuffer inBuff;
  size_t err, srcSize;

  if (!_ctx) {
    _ctx = ZSTD_createCCtx();
    if (!_ctx)
      return E_OUTOFMEMORY;

    _srcBuf = MyAlloc(_srcBufSize);
    if (!_srcBuf)
      return E_OUTOFMEMORY;

    _dstBuf = MyAlloc(_dstBufSize);
    if (!_dstBuf)
      return E_OUTOFMEMORY;

    /* setup level */
    err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_compressionLevel, _props._level);
    if (ZSTD_isError(err)) return E_INVALIDARG;

    /* setup thread count */
    err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_nbWorkers, _numThreads);
    if (ZSTD_isError(err)) return E_INVALIDARG;

    /* set the content size flag */
    err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_contentSizeFlag, 1);
    if (ZSTD_isError(err)) return E_INVALIDARG;

    /* enable ldm for large windowlog values */
    if (_WindowLog > 27 && _Long == 0)
      _Long = 1;

    /* set ldm */
    if (_Long == 1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_enableLongDistanceMatching, _Long);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_Strategy != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_compressionStrategy, _Strategy);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_WindowLog != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_windowLog, _WindowLog);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_HashLog != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_hashLog, _HashLog);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_ChainLog != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_chainLog, _ChainLog);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_SearchLog != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_searchLog, _SearchLog);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_SearchLength != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_minMatch, _SearchLength);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_TargetLen != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_targetLength, _TargetLen);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_OverlapLog != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_overlapSizeLog, _OverlapLog);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_LdmHashLog != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_ldmHashLog, _LdmHashLog);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_LdmSearchLength != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_ldmMinMatch, _LdmSearchLength);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_LdmBucketSizeLog != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_ldmBucketSizeLog, _LdmBucketSizeLog);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }

    if (_LdmHashEveryLog != -1) {
      err = ZSTD_CCtx_setParameter(_ctx, ZSTD_p_ldmHashEveryLog, _LdmHashEveryLog);
      if (ZSTD_isError(err)) return E_INVALIDARG;
    }
  }

  for (;;) {

    /* read input */
    srcSize = _srcBufSize;
    RINOK(ReadStream(inStream, _srcBuf, &srcSize));

    /* eof */
    if (srcSize == 0)
      ZSTD_todo = ZSTD_e_end;

    /* compress data */
    WaitForSingleObject(_hMutex, INFINITE);
    _processedIn += srcSize;
    ReleaseMutex(_hMutex);

    for (;;) {
      outBuff.dst = _dstBuf;
      outBuff.size = _dstBufSize;
      outBuff.pos = 0;

      if (ZSTD_todo == ZSTD_e_continue) {
        inBuff.src = _srcBuf;
        inBuff.size = srcSize;
        inBuff.pos = 0;
      } else {
        inBuff.src = 0;
        inBuff.size = srcSize;
        inBuff.pos = 0;
      }

      err = ZSTD_compress_generic(_ctx, &outBuff, &inBuff, ZSTD_todo);
      if (ZSTD_isError(err)) return E_FAIL;

#if DEBUG
      printf("err=%u ", (unsigned)err);
      printf("srcSize=%u ", (unsigned)srcSize);
      printf("todo=%u\n", ZSTD_todo);
      printf("inBuff.size=%u ", (unsigned)inBuff.size);
      printf("inBuff.pos=%u\n", (unsigned)inBuff.pos);
      printf("outBuff.size=%u ", (unsigned)outBuff.size);
      printf("outBuff.pos=%u\n\n", (unsigned)outBuff.pos);
      fflush(stdout);
#endif

      /* write output */
      if (outBuff.pos) {
        RINOK(WriteStream(outStream, _dstBuf, outBuff.pos));
        WaitForSingleObject(_hMutex, INFINITE);
        _processedOut += outBuff.pos;
        RINOK(progress->SetRatioInfo(&_processedIn, &_processedOut));
        ReleaseMutex(_hMutex);
      }

      /* done */
      if (ZSTD_todo == ZSTD_e_end && err == 0)
        return S_OK;

      /* need more input */
      if (inBuff.pos == inBuff.size)
        break;
    }
  }
}

STDMETHODIMP CEncoder::SetNumberOfThreads(UInt32 numThreads)
{
  const UInt32 kNumThreadsMax = ZSTD_THREAD_MAX;
  if (numThreads < 1) numThreads = 1;
  if (numThreads > kNumThreadsMax) numThreads = kNumThreadsMax;
  _numThreads = numThreads;
  return S_OK;
}

}}
#endif
