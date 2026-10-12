"""Nearest neighbour scaling. Changes nothing but the size, which makes it the reference for testing the pipeline."""

from PIL import Image

from texpipe.backends import Job, check_size


class NearestBackend:
    name = "nearest"

    def run(self, jobs: list[Job]) -> None:
        for job in jobs:
            with Image.open(job.source) as source:
                result = source.resize((source.width * job.scale, source.height * job.scale), Image.Resampling.NEAREST)
                check_size(result.size, source.size, job.scale)
            job.target.parent.mkdir(parents=True, exist_ok=True)
            result.save(job.target)
