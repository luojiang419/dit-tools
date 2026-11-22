const path = require("path");
const fs = require("fs");

const serverDir = __dirname;
const envFileName = process.env.DIT_FEEDBACK_ENV_FILE || ".env";
const envPath = path.join(serverDir, envFileName);

function loadEnvFile(filePath) {
  if (!fs.existsSync(filePath)) {
    return {};
  }

  const content = fs.readFileSync(filePath, "utf8");
  return content.split(/\r?\n/).reduce((result, line) => {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith("#")) {
      return result;
    }
    const separator = trimmed.indexOf("=");
    if (separator <= 0) {
      return result;
    }
    const key = trimmed.slice(0, separator).trim();
    const value = trimmed.slice(separator + 1).trim();
    result[key] = value;
    return result;
  }, {});
}

const envFile = loadEnvFile(envPath);
const appName = process.env.DIT_FEEDBACK_APP_NAME || envFile.DIT_FEEDBACK_APP_NAME || "dit-feedback-server";
const feedbackHost = envFile.DIT_FEEDBACK_HOST || "127.0.0.1";
const feedbackPort = envFile.DIT_FEEDBACK_PORT || "3020";

module.exports = {
  apps: [
    {
      name: appName,
      cwd: serverDir,
      script: path.join(serverDir, ".venv", "bin", "python"),
      args: `-m uvicorn app.main:app --host ${feedbackHost} --port ${feedbackPort}`,
      interpreter: "none",
      env: {
        DIT_FEEDBACK_ADMIN_USERNAME: envFile.DIT_FEEDBACK_ADMIN_USERNAME || "admin",
        DIT_FEEDBACK_ADMIN_PASSWORD: envFile.DIT_FEEDBACK_ADMIN_PASSWORD || "change-this-password",
        DIT_FEEDBACK_HOST: feedbackHost,
        DIT_FEEDBACK_PORT: feedbackPort,
        DIT_FEEDBACK_STORAGE_DIR: envFile.DIT_FEEDBACK_STORAGE_DIR || path.join(serverDir, "storage"),
        DIT_FEEDBACK_DB_PATH: envFile.DIT_FEEDBACK_DB_PATH || path.join(serverDir, "storage", "feedback.sqlite3"),
        DIT_FEEDBACK_PUBLIC_BASE_URL: envFile.DIT_FEEDBACK_PUBLIC_BASE_URL || "http://115.231.35.105:3020",
      },
    },
  ],
};
