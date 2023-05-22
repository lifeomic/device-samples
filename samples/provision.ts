/**
yarn provision \
  --claimCert=./certs/<cert_name>.pem \
  --claimKey=./certs/<key_name>.key \
  --serialNumber=12345 \
  --manufacturer=my-company \
  --name=my-device \
  --model=prototype
*/
import { parseArgs, ParseArgsConfig } from "util";
import { z } from "zod";
import { mqtt, iotidentity, iot } from "aws-iot-device-sdk-v2";
import { v4 } from "uuid";
import { DEFAULT_ENDPOINT } from "./common";
import fs from "fs";

const PROVISION_TEMPLATE_NAME = "DefaultProvisioningTemplate";

type Args = {
  iotEndpoint: string;
  claimCert: string;
  claimKey: string;
  serialNumber: string;
  manufacturer: string;
  name: string;
  model: string;
};

const getConnection = (args: Args) => {
  const configBuilder =
    iot.AwsIotMqttConnectionConfigBuilder.new_mtls_builder_from_path(
      args.claimCert,
      args.claimKey
    );

  configBuilder.with_clean_session(false);
  configBuilder.with_endpoint(args.iotEndpoint);
  const clientId = v4();
  configBuilder.with_client_id(clientId);
  const config = configBuilder.build();
  const client = new mqtt.MqttClient();
  return client.new_connection(config);
};

const fetchDeviceCertificate = async (
  identity: iotidentity.IotIdentityClient
): Promise<iotidentity.model.CreateKeysAndCertificateResponse | undefined> => {
  const keysSubRequest: iotidentity.model.CreateKeysAndCertificateSubscriptionRequest =
    {};

  const response = await new Promise<
    iotidentity.model.CreateKeysAndCertificateResponse | undefined
  >(async (resolve, reject) => {
    try {
      function keysAccepted(
        error?: iotidentity.IotIdentityError,
        response?: iotidentity.model.CreateKeysAndCertificateResponse
      ) {
        if (error || !response) {
          console.log("Error occurred with fetching certificate");
          reject(error);
        } else {
          resolve(response);
        }
      }

      function keysRejected(
        error?: iotidentity.IotIdentityError,
        response?: iotidentity.model.ErrorResponse
      ) {
        if (response) {
          console.log(
            "CreateKeysAndCertificate ErrorResponse",
            "statusCode=:",
            response.statusCode,
            "errorCode=:",
            response.errorCode,
            "errorMessage=:",
            response.errorMessage
          );
        }
        if (error) {
          console.log("Error occurred..");
        }
        reject(error);
      }

      await identity.subscribeToCreateKeysAndCertificateAccepted(
        keysSubRequest,
        mqtt.QoS.AtLeastOnce,
        (error, response) => keysAccepted(error, response)
      );

      await identity.subscribeToCreateKeysAndCertificateRejected(
        keysSubRequest,
        mqtt.QoS.AtLeastOnce,
        (error, response) => keysRejected(error, response)
      );

      console.log("Publishing to CreateKeysAndCertificate topic..");
      const keysRequest: iotidentity.model.CreateKeysAndCertificateRequest = {
        toJSON() {
          return {};
        },
      };

      await identity.publishCreateKeysAndCertificate(
        keysRequest,
        mqtt.QoS.AtLeastOnce
      );
    } catch (error) {
      reject(error);
    }
  });
  return response;
};

const registerDevice = async (
  identity: iotidentity.IotIdentityClient,
  certificateDetails: iotidentity.model.CreateKeysAndCertificateResponse,
  args: Args
): Promise<string | undefined> => {
  return await new Promise<string | undefined>(async (resolve, reject) => {
    try {
      function registerAccepted(
        error?: iotidentity.IotIdentityError,
        response?: iotidentity.model.RegisterThingResponse
      ) {
        if (error || !response) {
          console.log("Error occurred with registering device");
          reject(undefined);
        }
        resolve(response?.thingName);
      }

      function registerRejected(
        error?: iotidentity.IotIdentityError,
        response?: iotidentity.model.ErrorResponse
      ) {
        if (response) {
          console.log(
            "RegisterThing ErrorResponse for ",
            "statusCode=:",
            response.statusCode,
            "errorCode=:",
            response.errorCode,
            "errorMessage=:",
            response.errorMessage
          );
        }
        if (error) {
          console.log("Error occurred..");
        }
        resolve(undefined);
      }

      console.log(
        "Subscribing to RegisterThing Accepted and Rejected topics.."
      );
      const registerThingSubRequest: iotidentity.model.RegisterThingSubscriptionRequest =
        { templateName: PROVISION_TEMPLATE_NAME };
      await identity.subscribeToRegisterThingAccepted(
        registerThingSubRequest,
        mqtt.QoS.AtLeastOnce,
        (error, response) => registerAccepted(error, response)
      );

      await identity.subscribeToRegisterThingRejected(
        registerThingSubRequest,
        mqtt.QoS.AtLeastOnce,
        (error, response) => registerRejected(error, response)
      );

      console.log("Publishing to RegisterThing topic..");
      const map: { [key: string]: string } = {
        SerialNumber: args.serialNumber,
        Manufacturer: args.manufacturer,
        Name: args.name,
        Model: args.model,
      };

      const registerThing: iotidentity.model.RegisterThingRequest = {
        parameters: map,
        templateName: PROVISION_TEMPLATE_NAME,
        certificateOwnershipToken: certificateDetails.certificateOwnershipToken,
      };
      await identity.publishRegisterThing(registerThing, mqtt.QoS.AtLeastOnce);
    } catch (error) {
      reject(error);
    }
  });
};

const run = async (args: Args) => {
  const connection = getConnection(args);

  const identity = new iotidentity.IotIdentityClient(connection);

  await connection.connect();

  const certificateDetails = await fetchDeviceCertificate(identity);

  if (
    !certificateDetails?.certificateOwnershipToken ||
    !certificateDetails?.certificatePem ||
    !certificateDetails?.privateKey
  ) {
    console.error("Failed to fetch certificate details");
    process.exit(-1);
  }

  const thingName = await registerDevice(identity, certificateDetails, args);

  if (!thingName) {
    console.error("Failed to register device");
    process.exit(-1);
  }

  fs.writeFileSync(
    `./certs/${thingName}.pem`,
    certificateDetails.certificatePem
  );
  fs.writeFileSync(`./certs/${thingName}.key`, certificateDetails.privateKey);
  console.log(
    `Wrote certificate and key to \`./certs\` for device named: "${thingName}"`
  );

  await connection.disconnect();
};

const main = async () => {
  const options: ParseArgsConfig["options"] = {
    iotEndpoint: { type: "string", default: DEFAULT_ENDPOINT },
    claimCert: { type: "string" },
    claimKey: { type: "string" },
    serialNumber: { type: "string" },
    manufacturer: { type: "string" },
    name: { type: "string" },
    model: { type: "string" },
  };

  const argSchema = z
    .object({
      iotEndpoint: z.string(),
      claimCert: z.string(),
      claimKey: z.string(),
      serialNumber: z.string(),
      manufacturer: z.string(),
      name: z.string(),
      model: z.string(),
    })
    .transform((args) => {
      return {
        ...args,
      };
    });

  const { values } = parseArgs({ options, args: process.argv.slice(2) });
  const args = argSchema.parse(values);

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
