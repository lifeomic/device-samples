/*
example:

yarn firmwareUpdate \
  --deviceName=<device_name> \
*/
import { mqtt5, iot } from "aws-iot-device-sdk-v2";
import { ICrtError } from "aws-crt";
import { once, EventEmitter } from "events";
import { toUtf8 } from "@aws-sdk/util-utf8-browser";
import { parseArgs, ParseArgsConfig } from "util";
import { z } from "zod";
import { DEFAULT_ENDPOINT } from "./common";
import assert from "assert";
import axios from "axios";
import * as stream from "stream";
import { promisify } from "util";
import { createWriteStream } from "fs";

const finished = promisify(stream.finished);

function createClientConfig(args: {
  endpoint: string;
  cert: string;
  key: string;
  deviceName: string;
}): mqtt5.Mqtt5ClientConfig {
  const builder =
    iot.AwsIotMqtt5ClientConfigBuilder.newDirectMqttBuilderWithMtlsFromPath(
      args.endpoint,
      args.cert,
      args.key
    );

  builder.withConnectProperties({
    keepAliveIntervalSeconds: 1200,
    clientId: args.deviceName,
  });

  return builder.build();
}

type Args = {
  endpoint: string;
  deviceName: string;
};

function createClient(
  args: Args,
  internalEvents: EventEmitter
): mqtt5.Mqtt5Client {
  const config: mqtt5.Mqtt5ClientConfig = createClientConfig({
    cert: `./certs/${args.deviceName}.pem`,
    key: `./certs/${args.deviceName}.key`,
    deviceName: args.deviceName,
    endpoint: args.endpoint,
  });

  const client: mqtt5.Mqtt5Client = new mqtt5.Mqtt5Client(config);

  client.on("error", (error: ICrtError) => {
    console.log("Error event: " + error.toString());
  });

  client.on("attemptingConnect", (eventData: mqtt5.AttemptingConnectEvent) => {
    console.log("Attempting Connect event", eventData);
  });

  client.on("connectionSuccess", (eventData: mqtt5.ConnectionSuccessEvent) => {
    console.log("Connection Success event");
    console.log("Connack: " + JSON.stringify(eventData.connack));
    console.log("Settings: " + JSON.stringify(eventData.settings));
  });

  client.on("connectionFailure", (eventData: mqtt5.ConnectionFailureEvent) => {
    console.log("Connection failure event: ", eventData.error.toString());
    if (eventData.connack) {
      console.log("Connack: " + JSON.stringify(eventData.connack));
    }
  });

  client.on("disconnection", (eventData: mqtt5.DisconnectionEvent) => {
    console.log("Disconnection event: ", eventData.error.toString());
    if (eventData.disconnect !== undefined) {
      console.log("Disconnect packet: " + JSON.stringify(eventData.disconnect));
    }
  });

  client.on("stopped", (eventData: mqtt5.StoppedEvent) => {
    console.log("Stopped event", eventData);
  });

  return client;
}

async function downloadFile(
  fileUrl: string,
  outputLocation: string
): Promise<any> {
  const writer = createWriteStream(outputLocation);
  return axios({
    method: "get",
    url: fileUrl,
    responseType: "stream",
  }).then((response) => {
    response.data.pipe(writer);
    return finished(writer); //this is a Promise
  });
}

const mqttRequestResponse = async (
  client: mqtt5.Mqtt5Client,
  topic: string,
  payload: string
) => {
  await client.subscribe({
    subscriptions: [
      {
        qos: mqtt5.QoS.AtLeastOnce,
        topicFilter: `${topic}/accepted`,
      },
      {
        qos: mqtt5.QoS.AtLeastOnce,
        topicFilter: `${topic}/rejected`,
      },
    ],
  });
  await client.publish({
    qos: mqtt5.QoS.AtMostOnce,
    topicName: topic,
    payload,
  });

  const messageReceived = once(client, "messageReceived");
  const response = await messageReceived;

  assert(response.length === 1);
  const message = response[0] as mqtt5.MessageReceivedEvent;
  assert(message.message.topicName === `${topic}/accepted`);

  await client.unsubscribe({
    topicFilters: [`${topic}/accepted`, `${topic}/rejected`],
  });

  return JSON.parse(
    toUtf8(new Uint8Array(message.message.payload as ArrayBuffer))
  );
};

type JobsResponse = {
  timestamp: number;
  inProgressJobs: {
    jobId: string;
    queuedAt: number;
    lastUpdatedAt: number;
    startedAt: number;
    executionNumber: number;
    versionNumber: number;
  }[];
  queuedJobs: {
    jobId: string;
    queuedAt: number;
    lastUpdatedAt: number;
    executionNumber: number;
    versionNumber: number;
  }[];
};

const checkJobs = async (client: mqtt5.Mqtt5Client, deviceName: string) => {
  const topic = `$aws/things/${deviceName}/jobs/get`;

  const jobs = (await mqttRequestResponse(
    client,
    topic,
    JSON.stringify({})
  )) as JobsResponse;

  console.log(
    "Found",
    jobs.inProgressJobs.length,
    "jobs in progress and",
    jobs.queuedJobs.length,
    "jobs queued."
  );
  return jobs;
};

type JobDetailResponse = {
  timestamp: number;
  execution: {
    jobId: string;
    status:
      | "QUEUED"
      | "IN_PROGRESS"
      | "FAILED"
      | "SUCCEEDED"
      | "CANCELED"
      | "TIMED_OUT"
      | "REJECTED"
      | "REMOVED";
    queuedAt: number;
    lastUpdatedAt: number;
    versionNumber: number;
    executionNumber: number;
    jobDocument: {
      operation: "firmware_update";
      deploymentId: string;
      firmwareId: string;
      firmwareUrl: string;
    };
  };
};

const getJobDetail = async (
  client: mqtt5.Mqtt5Client,
  deviceName: string,
  jobId: string
) => {
  const topic = `$aws/things/${deviceName}/jobs/${jobId}/get`;

  const jobDetail = (await mqttRequestResponse(
    client,
    topic,
    JSON.stringify({})
  )) as JobDetailResponse;

  console.log(
    "Job status:",
    jobDetail.execution.status,
    ", document:",
    jobDetail.execution.jobDocument
  );
  return jobDetail;
};

type SetJobStatusResponse = {
  timestamp: number;
};

const setJobStatus = async (
  client: mqtt5.Mqtt5Client,
  deviceName: string,
  jobId: string,
  jobStatus: "IN_PROGRESS" | "FAILED" | "SUCCEEDED" | "REJECTED",
  jobStatusDetails?: { percentProgress: number }
) => {
  const topic = `$aws/things/${deviceName}/jobs/${jobId}/update`;

  const statusResponse = (await mqttRequestResponse(
    client,
    topic,
    JSON.stringify({
      status: jobStatus,
      ...(jobStatusDetails ? { statusDetails: jobStatusDetails } : null),
    })
  )) as SetJobStatusResponse;
  return statusResponse;
};

const run = async (args: Args) => {
  const internalEvents: EventEmitter = new EventEmitter();
  const client: mqtt5.Mqtt5Client = createClient(args, internalEvents);

  const connectionSuccess = once(client, "connectionSuccess");
  client.start();
  await connectionSuccess;

  const jobs = await checkJobs(client, args.deviceName);

  if (jobs.inProgressJobs.length > 0) {
    console.log("Finishing up in progress job");
    const jobSummary = jobs.inProgressJobs[0];

    await setJobStatus(client, args.deviceName, jobSummary.jobId, "SUCCEEDED", {
      percentProgress: 100,
    });
    console.log("Marked job as success");
  } else if (jobs.queuedJobs.length > 0) {
    console.log("Starting next queued job");
    const jobSummary = jobs.queuedJobs[0];

    const jobDetail = await getJobDetail(
      client,
      args.deviceName,
      jobSummary.jobId
    );

    if (jobDetail.execution.jobDocument.operation === "firmware_update") {
      // Firmware install operation
      // Set job in progress
      await setJobStatus(
        client,
        args.deviceName,
        jobSummary.jobId,
        "IN_PROGRESS",
        {
          percentProgress: 0,
        }
      );

      // Download file
      const downloadLocation = `./data/${jobDetail.execution.jobDocument.firmwareId}`;
      await downloadFile(
        jobDetail.execution.jobDocument.firmwareUrl,
        downloadLocation
      );
      console.log("Downloaded file to", downloadLocation);

      // Update jobs progress
      await setJobStatus(
        client,
        args.deviceName,
        jobSummary.jobId,
        "IN_PROGRESS",
        {
          percentProgress: 50,
        }
      );

      // Run install, TODO: your logic here
      await new Promise((r) => setTimeout(r, 1000));

      // Set job succeeded
      await setJobStatus(
        client,
        args.deviceName,
        jobSummary.jobId,
        "SUCCEEDED",
        {
          percentProgress: 100,
        }
      );
      console.log("Marked job as success");
    } else {
      // Ignore other jobs, mark them finished
      console.log("Job operation not supported, rejecting");
      await setJobStatus(client, args.deviceName, jobSummary.jobId, "REJECTED");
      console.log("Job was rejected");
    }

    if (jobs.queuedJobs.length > 1) {
      console.log(jobs.queuedJobs.length - 1, "jobs still remain.");
    } else {
      console.log("Job finished and was the last pending job.");
    }
  } else {
    console.log("No jobs need to run, device is up to date!");
  }

  const stopped = once(client, "stopped");
  client.stop();
  await stopped;
  client.close();
};

const main = async () => {
  const options: ParseArgsConfig["options"] = {
    endpoint: { type: "string", default: DEFAULT_ENDPOINT },
    deviceName: { type: "string" },
  };

  const argSchema = z
    .object({
      endpoint: z.string(),
      deviceName: z.string(),
    })
    .transform((args) => {
      return {
        ...args,
        payload: {},
      };
    });

  const { values } = parseArgs({ options, args: process.argv.slice(2) });
  const args = argSchema.parse({
    ...values,
  });

  await run(args);
  process.exit(0);
};

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
