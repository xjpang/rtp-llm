import logging
import torch
import os
import time
import threading
import traceback
import asyncio
from enum import Enum
from typing import Iterator, List, Optional, Tuple, Union, Any, Dict, AsyncGenerator
from maga_transformer.config.gpt_init_model_parameters import GptInitModelParameters
from maga_transformer.distribute.worker_info import g_parallel_info
from maga_transformer.metrics import GaugeMetrics, kmonitor
from maga_transformer.utils.time_util import Timer

from maga_transformer.models.base_model import BaseModel
from maga_transformer.async_decoder_engine.embedding.embedding_scheduler import EmbeddingScheduler
from maga_transformer.async_decoder_engine.embedding.embedding_model_executor import EmbeddingModelExecutor
from maga_transformer.async_decoder_engine.embedding.embedding_stream import EmbeddingStream, EmbeddingOutput, EmbeddingInput

class EmbeddingDecoderEngine(object):
    def __init__(self, config: GptInitModelParameters, model: BaseModel):
        self.config_ = config
        self.scheduler_ = EmbeddingScheduler(self.config_)
        self.executor_ = EmbeddingModelExecutor(model, config)
        self.start()

    async def decode(self, input: List[EmbeddingInput]) -> List[EmbeddingOutput]:
        streams = self.scheduler_.enqueue(input)
        return await self._generate_loop(streams)

    async def _generate_loop(self, streams: List[EmbeddingStream]) -> List[EmbeddingOutput]:
        finished: List[bool] = [False] * len(streams)
        while True:
            for index, stream in enumerate(streams):
                if stream.error_info != "":
                    raise Exception(stream.error_info)
                if stream.finished:
                    finished[index] = True
            if all(finished):
                break
            await asyncio.sleep(0.001)

        return [stream.output for stream in streams]

    @torch.inference_mode()
    def step(self):
        batch_query = None
        try:
            with Timer() as t:
                batch_query = self.scheduler_.schedule()
                if batch_query.total_batch_size == 0 and g_parallel_info.tp_rank == 0:
                    torch.cuda.nvtx.range_pop()
                    time.sleep(0.001)
                    return
                print("total_batch_size: ", batch_query.total_batch_size)
                batch_query.generate_model_input()
                batch_query.tp_sync()
                self.executor_.process(batch_query)

                if g_parallel_info.tp_rank == 0:
                    batch_query.update_streams()
            # self.report_metric(t.cost_ms())

        except Exception as e:
            if batch_query:
                batch_query.update_all_errors(str(e))
            logging.error(
                f'process run error: {e}, Traceback: {traceback.format_exc()}'
            )
            if (g_parallel_info.tp_size) > 1 or ("CUDA" in str(e)):
                kmonitor.report(GaugeMetrics.ERROR_EXIT_METRIC, 1)
                kmonitor.flush()
                time.sleep(0.1)
                # NOTE: nccl could hang when any error. GPU may hang under CUDA error.
                os._exit(-1)

    def start(self):
        self.need_stop_ = False
        self.thread = threading.Thread(target=self.run_engine, daemon=True)
        self.thread.start()

    def stop(self):
        logging.info("decoder engine begin stop")
        self.need_stop_ = True
        self.thread.join()
        logging.info("decoder engine stop done")


    def run_engine(self):
        while not self.need_stop_:
            self.step()
        logging.info("need stop flag is true, exit run_engine")