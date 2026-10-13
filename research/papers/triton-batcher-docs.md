# NVIDIA Triton Inference Server — Batchers user guide (saved copy for citation verification)

Source: https://docs.nvidia.com/deeplearning/triton-inference-server/user-guide/docs/user_guide/batcher.html
Fetched: 2026-08-14. This file is a verbatim copy of the rendered documentation page, used only for citation verification in research notes.

Archive note (post-contest): retained verbatim as fetched; SA01–SA10 program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56. Prepared by Ntokozo Dlamini with assistant support.

---

# Batchers

## Dynamic Batcher

Dynamic batching is a feature of Triton that allows inference requests to be combined by the server, so that a batch is created dynamically. Creating a batch of requests typically results in increased throughput. The dynamic batcher should be used for stateless models. The dynamically created batches are distributed to all model instances configured for the model.

Dynamic batching is enabled and configured independently for each model using the *ModelDynamicBatching* property in the model configuration. These settings control the preferred size(s) of the dynamically created batches, the maximum time that requests can be delayed in the scheduler to allow other requests to join the dynamic batch, and queue properties such a queue size, priorities, and time-outs.

### Recommended Configuration Process

The following steps are the recommended process for tuning the dynamic batcher for each model. It is also possible to use the Model Analyzer to automatically search across different dynamic batcher configurations.

- Decide on a maximum batch size for the model.
- Add the following to the model configuration to enable the dynamic batcher with all default settings. By default the dynamic batcher will create batches as large as possible up to the maximum batch size and will not delay when forming batches.

```
dynamic_batching { }

```

- Use the Performance Analyzer to determine the latency and throughput provided by the default dynamic batcher configuration.
- If the default configuration results in latency values that are within your latency budget, try one or both of the following to trade off increased latency for increased throughput:
  - Increase maximum batch size.
  - Set batch delay to a non-zero value. Try increasing delay values until the latency budget is exceeded to see the impact on throughput.
- Preferred batch sizes should not be used for most models. A preferred batch size(s) should only be configured if that batch size results in significantly higher performance than other batch sizes.

### Preferred Batch Sizes

The *preferred_batch_size* property indicates the batch sizes that the dynamic batcher should attempt to create. For most models, *preferred_batch_size* should not be specified, as described in Recommended Configuration Process. An exception is TensorRT models that specify multiple optimization profiles for different batch sizes. In this case, because some optimization profiles may give significant performance improvement compared to others, it may make sense to use *preferred_batch_size* for the batch sizes supported by those higher-performance optimization profiles.

The following example shows the configuration that enables dynamic batching with preferred batch sizes of 4 and 8.

```
dynamic_batching {
  preferred_batch_size: [ 4, 8 ]
}

```

When a model instance becomes available for inferencing, the dynamic batcher will attempt to create batches from the requests that are available in the scheduler. Requests are added to the batch in the order the requests were received. If the dynamic batcher can form a batch of a preferred size(s) it will create a batch of the largest possible preferred size and send it for inferencing.
If the dynamic batcher cannot form a batch of a preferred size (or if the dynamic batcher is not configured with any preferred batch sizes), it will send a batch of the largest size possible that is less than the maximum batch size allowed by the model (but see the following section for the delay option that changes this behavior).

### Delayed Batching

The dynamic batcher can be configured to allow requests to be delayed for a limited time in the scheduler to allow other requests to join the dynamic batch. For example, the following configuration sets the maximum delay time of 100 microseconds for a request.

```
dynamic_batching {
  max_queue_delay_microseconds: 100
}

```

The *max_queue_delay_microseconds* property setting changes the dynamic batcher behavior when a maximum size (or preferred size) batch cannot be created. When a batch of a maximum or preferred size cannot be created from the available requests, the dynamic batcher will delay sending the batch as long as no request is delayed longer than the configured *max_queue_delay_microseconds* value. If a new request arrives during this delay and allows the dynamic batcher to form a batch of a maximum or preferred batch size, then that batch is sent immediately for inferencing.
If the delay expires the dynamic batcher sends the batch as is, even though it is not a maximum or preferred size.

### Queue Policy

The *ModelQueuePolicy* property allows a maximum queue size to be set using the *max_queue_size*. The *timeout_action*, *default_timeout_microseconds* and *allow_timeout_override* settings allow the queue to be configured so that individual requests are rejected or deferred if their time in the queue exceeds a specified timeout.

## Iterative Sequences

For models and backends that support "iterative sequences", users can enable support in the sequence batcher by specifying:

```
sequence_batching {
  iterative_sequence: true
}

```

An "iterative sequence" refers to stateful models that iteratively process a single request until a complete response is generated. When iterative sequence is enabled, the sequence scheduler will expect a single incoming request to initiate the sequence. Backends that support iterative sequences can then yield back to the sequence batcher to reschedule the request for further execution in a future batch.

### Continuous/Inflight Batching with Iterative Sequences

Continuous batching, iteration level batching, and inflight batching are terms used in large language model (LLM) inferencing to describe batching strategies that form batches of requests at each iteration step. By forming batches "continuously" inference servers can increase throughput by reusing batch slots as soon as they are free without waiting for all requests in a batch to complete.

As the number of steps required to process a request can vary significantly, batching existing requests and new requests continuously can have a significant improvement on throughput and latency.

To achieve inflight batching with iterative sequences, the backend should break request processing into a number of steps, where each step corresponds to one Triton model instance execution. At the end of each step, the model instance will release requests that have been completed and reschedule requests that are still inflight. Triton will then form and schedule the next batch of requests that mixes new and rescheduled requests.
