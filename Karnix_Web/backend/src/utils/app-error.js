export class AppError extends Error {
  constructor(statusCode, code, message) {
    super(message);
    this.name = "AppError";
    this.statusCode = statusCode;
    this.code = code;
  }
}

export const badRequest = (code, message) => new AppError(400, code, message);
export const unauthorized = (code = "UNAUTHORIZED", message = "Authentication required") =>
  new AppError(401, code, message);
export const conflict = (code, message) => new AppError(409, code, message);
