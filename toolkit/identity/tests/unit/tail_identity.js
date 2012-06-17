
// pre-emptively shut down to clear resources
if (typeof IdentityService !== "undefined")
  IdentityService.shutdown();
if (typeof IDService !== "undefined")
  IDService.shutdown();


