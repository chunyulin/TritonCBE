#!/usr/bin/python

# Copyright (c) 2019-2020, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""
Testing script modified from https://github.com/triton-inference-server/server/blob/r21.02/qa/L0_backend_identity/identity_test.py
"""

import argparse
import numpy as np
import os
import re
import sys
import requests as httpreq
from builtins import range
import tritongrpcclient as grpcclient
import tritonhttpclient as httpclient
from tritonclientutils import np_to_triton_dtype
from collections import OrderedDict

FLAGS = None

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-v',
                        '--verbose',
                        action="store_true",
                        required=False,
                        default=False,
                        help='Enable verbose output')
    parser.add_argument('-u',
                        '--url',
                        type=str,
                        required=False,
                        help='Inference server URL.')
    parser.add_argument(
        '-i',
        '--protocol',
        type=str,
        required=False,
        default='http',
        help='Protocol ("http"/"grpc") used to ' +
        'communicate with inference service. Default is "http".')

    FLAGS = parser.parse_args()
    if (FLAGS.protocol != "http") and (FLAGS.protocol != "grpc"):
        print("unexpected protocol \"{}\", expects \"http\" or \"grpc\"".format(
            FLAGS.protocol))
        exit(1)

    client_util = httpclient if FLAGS.protocol == "http" else grpcclient

    if FLAGS.url is None:
        FLAGS.url = "localhost:8020" if FLAGS.protocol == "http" else "localhost:8021"

    model_name = "DigiGPU"
    request_parallelism = 1
    with client_util.InferenceServerClient(FLAGS.url,
                                           concurrency=request_parallelism,
                                           verbose=FLAGS.verbose) as client:
        requests = []
        inputs = []
        for i in range(request_parallelism):
            input_f5_stride = np.array([3], dtype=np.uint32)
            input_f01_stride = np.array([4], dtype=np.uint32)
            input_f3_stride = np.array([5], dtype=np.uint32)

            input_f5_ids = np.array([1,2], dtype=np.uint32)
            input_f5_data = np.array([1,2,3,4], dtype=np.uint16)
            input_f5_npresamples = np.array([1,2,4,5,6,8,10], dtype=np.uint8)

            input_f01_ids = np.array([1,2], dtype=np.uint32)
            input_f01_data = np.array([1,2,3,4], dtype=np.uint16)

            input_f3_ids = np.array([1,2], dtype=np.uint32)
            input_f3_data = np.array([1,2,3,4], dtype=np.uint16)

            inputmap = OrderedDict()
            inputmap['F5_STRIDE'] = input_f5_stride
            inputmap['F5_IDS'] = input_f5_ids
            inputmap['F5_DATA'] = input_f5_data
            inputmap['F5_NPRESAMPLES'] = input_f5_npresamples
            inputmap['F01_STRIDE'] = input_f01_stride
            inputmap['F01_IDS'] = input_f01_ids
            inputmap['F01_DATA'] = input_f01_data
            inputmap['F3_STRIDE'] = input_f3_stride
            inputmap['F3_IDS'] = input_f3_ids
            inputmap['F3_DATA'] = input_f3_data

            for iname, idata in inputmap.items():
                inputs.append(client_util.InferInput(iname, idata.shape, np_to_triton_dtype(idata.dtype)))
                inputs[-1].set_data_from_numpy(idata)

            requests.append(client.async_infer(model_name, inputs))

        for i in range(request_parallelism):
            # Get the result from the initiated asynchronous inference request.
            # Note the call will block till the server responds.
            results = requests[i].get_result()
            print(results)

            output_data = results.as_numpy("OUTPUT0")
            if output_data is None:
                print("error: expected 'OUTPUT0'")
                sys.exit(1)

            print("output data: ", output_data)


    print("Passed all tests!")
