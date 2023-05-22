/*
example:

yarn uploadFile \
  --deviceName=<device_name> \
  --fileName=test \
  --filePath=./data/pic.png
*/
import { mqtt5, iot } from "aws-iot-device-sdk-v2";
import { ICrtError } from "aws-crt";
import { once, EventEmitter } from "events";
import { toUtf8 } from "@aws-sdk/util-utf8-browser";
import { parseArgs, ParseArgsConfig } from "util";
import { z } from "zod";
import axios from "axios";
import fs from "fs";
import { DEFAULT_ENDPOINT } from "./common";
import mime from "mime-types";

const FILE_UPLOAD_URL_FINISHED = "FileUploadUrlFinished";

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
  topic: string;
  endpoint: string;
  payload: {
    fileName: string;
    contentType: string;
  };
  deviceName: string;
  filePath: string;
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

  client.on(
    "messageReceived",
    (eventData: mqtt5.MessageReceivedEvent): void => {
      console.log("Received upload URL");
      const payload = JSON.parse(
        toUtf8(new Uint8Array(eventData.message.payload as ArrayBuffer))
      ) as { uploadUrl: string };

      fs.promises
        .readFile(args.filePath)
        .then((file) => {
          return axios.put(payload.uploadUrl, file, {
            headers: { "Content-Type": args.payload.contentType },
          });
        })
        .then((response) => {
          console.log("Upload response status", response.status);
          internalEvents.emit(FILE_UPLOAD_URL_FINISHED);
        })
        .catch((e) => console.error("failed to read file", e));
    }
  );

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

const run = async (args: Args) => {
  const internalEvents: EventEmitter = new EventEmitter();
  const client: mqtt5.Mqtt5Client = createClient(args, internalEvents);

  const connectionSuccess = once(client, "connectionSuccess");
  client.start();
  await connectionSuccess;

  const suback = await client.subscribe({
    subscriptions: [
      { qos: mqtt5.QoS.AtLeastOnce, topicFilter: args.deviceName },
    ],
  });
  console.log("Suback result: " + JSON.stringify(suback));

  const publishResult = await client.publish({
    qos: mqtt5.QoS.AtMostOnce,
    topicName: args.topic,
    payload: JSON.stringify(args.payload),
    userProperties: [],
  });
  console.log("Publish result: " + JSON.stringify(publishResult));

  const message = once(client, "messageReceived");
  await message;

  const fileUploaded = once(internalEvents, FILE_UPLOAD_URL_FINISHED);
  await fileUploaded;

  const stopped = once(client, "stopped");

  client.stop();

  await stopped;

  client.close();
};

const main = async () => {
  const options: ParseArgsConfig["options"] = {
    topic: { type: "string", default: "$aws/rules/CreateFileUploadLink/" },
    endpoint: { type: "string", default: DEFAULT_ENDPOINT },
    deviceName: { type: "string" },
    fileName: { type: "string" },
    filePath: { type: "string" },
  };

  const argSchema = z
    .object({
      topic: z.string(),
      endpoint: z.string(),
      deviceName: z.string(),
      fileName: z.string(),
      filePath: z.string(),
    })
    .transform((args) => {
      const contentType = mime.lookup(args.filePath);
      if (!contentType) {
        console.error("Failed to get file content type");
        process.exit(-1);
      }
      return {
        ...args,
        payload: {
          fileName: args.fileName,
          contentType,
        },
      };
    });

  const { values } = parseArgs({ options, args: process.argv.slice(2) });
  const args = argSchema.parse({
    ...values,
  });

  // make it wait as long as possible once the promise completes we'll turn it off.
  const timer = setTimeout(() => {}, 2147483647);

  await run(args);

  clearTimeout(timer);

  process.exit(0);
};

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
